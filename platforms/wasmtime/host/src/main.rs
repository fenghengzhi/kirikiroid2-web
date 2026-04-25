use std::path::{Path, PathBuf};

use anyhow::{anyhow, bail, Context, Result};
use clap::Parser;
use serde_json::{json, Value};
use wasmtime::{Config, Engine, Instance, Linker, Memory, Module, OptLevel, Store, TypedFunc};
use wasmtime_wasi::p1::{self, WasiP1Ctx};
use wasmtime_wasi::{DirPerms, FilePerms, WasiCtxBuilder};

#[derive(Parser, Debug)]
#[command(name = "krkr2_wasmtime_host")]
#[command(about = "Headless Wasmtime host for the KrKr2 wasm guest")]
struct Args {
    #[arg(long)]
    wasm: PathBuf,

    #[arg(long)]
    repo_root: PathBuf,

    #[arg(long)]
    xp3: PathBuf,

    #[arg(long, default_value_t = 1)]
    frames: u32,

    #[arg(long, default_value_t = 16.666_666_7)]
    dt_ms: f64,

    #[arg(long, default_value = "log")]
    trace: String,

    #[arg(long)]
    json: bool,
}

struct Guest {
    store: Store<WasiP1Ctx>,
    memory: Memory,
    malloc: TypedFunc<i32, i32>,
    free: TypedFunc<i32, ()>,
    init: TypedFunc<(i32, i32), i32>,
    startup_from: TypedFunc<(i32, i32), i32>,
    tick: TypedFunc<f64, i32>,
    set_trace: TypedFunc<i32, ()>,
    get_trace_ptr: TypedFunc<(), i32>,
    get_trace_len: TypedFunc<(), i32>,
    get_error_ptr: TypedFunc<(), i32>,
    get_error_len: TypedFunc<(), i32>,
    get_framebuffer_ptr: TypedFunc<(), i32>,
    get_framebuffer_width: TypedFunc<(), i32>,
    get_framebuffer_height: TypedFunc<(), i32>,
    get_framebuffer_pitch: TypedFunc<(), i32>,
    get_framebuffer_format: TypedFunc<(), i32>,
    get_framebuffer_frame_no: TypedFunc<(), i32>,
}

impl Guest {
    fn instantiate(args: &Args) -> Result<Self> {
        let mut config = Config::new();
        config.debug_info(true);
        config.cranelift_opt_level(OptLevel::None);
        config.wasm_exceptions(true);
        config.wasm_simd(true);
        config.wasm_threads(true);

        let engine = Engine::new(&config)?;
        let module = Module::from_file(&engine, &args.wasm)
            .map_err(|err| anyhow!("loading wasm {}: {err}", args.wasm.display()))?;

        let repo_root = args
            .repo_root
            .canonicalize()
            .with_context(|| format!("canonicalizing repo root {}", args.repo_root.display()))?;
        let mut wasi_builder = WasiCtxBuilder::new();
        wasi_builder.inherit_stdout().inherit_stderr();
        wasi_builder
            .preopened_dir(&repo_root, ".", DirPerms::READ, FilePerms::READ)
            .map_err(|err| anyhow!("preopening {}: {err}", repo_root.display()))?;
        let wasi = wasi_builder.build_p1();
        let mut store = Store::new(&engine, wasi);

        let mut linker = Linker::<WasiP1Ctx>::new(&engine);
        p1::add_to_linker_sync(&mut linker, |ctx| ctx)?;
        define_emscripten_stubs(&mut linker)?;

        let instance = linker.instantiate(&mut store, &module)?;
        run_initializer(&mut store, &instance)?;

        let memory = instance
            .get_memory(&mut store, "memory")
            .context("guest did not export memory")?;
        Ok(Self {
            malloc: instance.get_typed_func(&mut store, "malloc")?,
            free: instance.get_typed_func(&mut store, "free")?,
            init: instance.get_typed_func(&mut store, "krkr2_wasm_init")?,
            startup_from: instance.get_typed_func(&mut store, "krkr2_wasm_startup_from")?,
            tick: instance.get_typed_func(&mut store, "krkr2_wasm_tick")?,
            set_trace: instance.get_typed_func(&mut store, "krkr2_wasm_set_trace")?,
            get_trace_ptr: instance.get_typed_func(&mut store, "krkr2_wasm_get_trace_ptr")?,
            get_trace_len: instance.get_typed_func(&mut store, "krkr2_wasm_get_trace_len")?,
            get_error_ptr: instance.get_typed_func(&mut store, "krkr2_wasm_get_error_ptr")?,
            get_error_len: instance.get_typed_func(&mut store, "krkr2_wasm_get_error_len")?,
            get_framebuffer_ptr: instance.get_typed_func(
                &mut store,
                "krkr2_wasm_get_framebuffer_ptr",
            )?,
            get_framebuffer_width: instance.get_typed_func(
                &mut store,
                "krkr2_wasm_get_framebuffer_width",
            )?,
            get_framebuffer_height: instance.get_typed_func(
                &mut store,
                "krkr2_wasm_get_framebuffer_height",
            )?,
            get_framebuffer_pitch: instance.get_typed_func(
                &mut store,
                "krkr2_wasm_get_framebuffer_pitch",
            )?,
            get_framebuffer_format: instance.get_typed_func(
                &mut store,
                "krkr2_wasm_get_framebuffer_format",
            )?,
            get_framebuffer_frame_no: instance.get_typed_func(
                &mut store,
                "krkr2_wasm_get_framebuffer_frame_no",
            )?,
            store,
            memory,
        })
    }

    fn with_guest_bytes<T>(
        &mut self,
        bytes: &[u8],
        f: impl FnOnce(&mut Store<WasiP1Ctx>, i32, i32) -> Result<T>,
    ) -> Result<T> {
        let ptr = self.malloc.call(&mut self.store, bytes.len() as i32)?;
        if ptr == 0 && !bytes.is_empty() {
            bail!("guest malloc failed");
        }
        self.memory
            .write(&mut self.store, ptr as usize, bytes)
            .context("writing guest memory")?;
        let result = f(&mut self.store, ptr, bytes.len() as i32);
        self.free.call(&mut self.store, ptr)?;
        result
    }

    fn read_string(
        &mut self,
        ptr_func: &TypedFunc<(), i32>,
        len_func: &TypedFunc<(), i32>,
    ) -> Result<String> {
        let ptr = ptr_func.call(&mut self.store, ())?;
        let len = len_func.call(&mut self.store, ())?;
        if ptr == 0 || len <= 0 {
            return Ok(String::new());
        }
        let data = self.memory.data(&self.store);
        let start = ptr as usize;
        let end = start
            .checked_add(len as usize)
            .context("guest string range overflow")?;
        if end > data.len() {
            bail!("guest string range out of bounds: {start}..{end}");
        }
        Ok(String::from_utf8_lossy(&data[start..end]).into_owned())
    }

    fn framebuffer_summary(&mut self) -> Result<Value> {
        let ptr = self.get_framebuffer_ptr.call(&mut self.store, ())?;
        let width = self.get_framebuffer_width.call(&mut self.store, ())?;
        let height = self.get_framebuffer_height.call(&mut self.store, ())?;
        let pitch = self.get_framebuffer_pitch.call(&mut self.store, ())?;
        let format = self.get_framebuffer_format.call(&mut self.store, ())?;
        let frame_no = self.get_framebuffer_frame_no.call(&mut self.store, ())?;
        Ok(json!({
            "ptr": ptr,
            "width": width,
            "height": height,
            "pitch": pitch,
            "format": format,
            "frameNo": frame_no
        }))
    }
}

fn define_emscripten_stubs(linker: &mut Linker<WasiP1Ctx>) -> Result<()> {
    linker.func_wrap("env", "emscripten_asm_const_int", |_code: i32, _argv: i32, _argc: i32| -> i32 {
        0
    })?;
    linker.func_wrap(
        "env",
        "js_decode_text",
        |_src: i32, _src_len: i32, _dst: i32, _dst_len: i32, _flags: i32| -> i32 { 0 },
    )?;
    linker.func_wrap("env", "emscripten_notify_memory_growth", |_index: i32| {})?;
    linker.func_wrap(
        "env",
        "__syscall_getdents64",
        |_fd: i32, _dirp: i32, _count: i32| -> i32 { -52 },
    )?;
    linker.func_wrap(
        "env",
        "__syscall_unlinkat",
        |_dirfd: i32, _path: i32, _flags: i32| -> i32 { -52 },
    )?;
    linker.func_wrap("env", "__syscall_rmdir", |_path: i32| -> i32 { -52 })?;
    Ok(())
}

fn run_initializer(store: &mut Store<WasiP1Ctx>, instance: &Instance) -> Result<()> {
    for name in ["__initialize", "_initialize"] {
        if let Ok(init) = instance.get_typed_func::<(), ()>(&mut *store, name) {
            init.call(store, ())?;
            return Ok(());
        }
    }
    Ok(())
}

fn trace_mask(trace: &str) -> i32 {
    trace
        .split(',')
        .map(str::trim)
        .fold(0, |mask, item| match item {
            "motion" => mask | 1,
            "log" => mask | 2,
            "framebuffer" => mask | 4,
            _ => mask,
        })
}

fn guest_xp3_path(repo_root: &Path, xp3: &Path) -> Result<String> {
    let repo_root = repo_root.canonicalize()?;
    let xp3_abs = if xp3.is_absolute() {
        xp3.to_path_buf()
    } else {
        repo_root.join(xp3)
    }
    .canonicalize()
    .with_context(|| format!("canonicalizing xp3 {}", xp3.display()))?;

    let rel = xp3_abs.strip_prefix(&repo_root).with_context(|| {
        format!(
            "xp3 {} must be inside repo root {} for the headless WASI preopen",
            xp3_abs.display(),
            repo_root.display()
        )
    })?;
    Ok(rel.to_string_lossy().replace('\\', "/"))
}

fn main() -> Result<()> {
    let args = Args::parse();
    let guest_path = guest_xp3_path(&args.repo_root, &args.xp3)?;
    let mut guest = Guest::instantiate(&args)?;

    let mask = trace_mask(&args.trace);
    guest.set_trace.call(&mut guest.store, mask)?;

    let config = json!({
        "repoRoot": args.repo_root,
        "xp3": guest_path,
        "trace": args.trace,
        "headless": true
    })
    .to_string();
    let init_func = guest.init.clone();
    let init_ok = guest.with_guest_bytes(config.as_bytes(), |store, ptr, len| {
        Ok(init_func.call(store, (ptr, len))? != 0)
    })?;

    let startup_func = guest.startup_from.clone();
    let startup_ok = guest.with_guest_bytes(guest_path.as_bytes(), |store, ptr, len| {
        Ok(startup_func.call(store, (ptr, len))? != 0)
    })?;

    let mut tick_ok = true;
    for _ in 0..args.frames {
        if guest.tick.call(&mut guest.store, args.dt_ms)? == 0 {
            tick_ok = false;
            break;
        }
    }

    let get_error_ptr = guest.get_error_ptr.clone();
    let get_error_len = guest.get_error_len.clone();
    let error = guest.read_string(&get_error_ptr, &get_error_len)?;
    let get_trace_ptr = guest.get_trace_ptr.clone();
    let get_trace_len = guest.get_trace_len.clone();
    let trace_raw = guest.read_string(&get_trace_ptr, &get_trace_len)?;
    let trace_json: Value = serde_json::from_str(&trace_raw).unwrap_or_else(|_| json!([]));
    let framebuffer = guest.framebuffer_summary()?;
    let ok = init_ok && startup_ok && tick_ok && error.is_empty();

    let report = json!({
        "ok": ok,
        "initOk": init_ok,
        "startupOk": startup_ok,
        "tickOk": tick_ok,
        "error": error,
        "guestXp3": guest_path,
        "trace": trace_json,
        "framebuffer": framebuffer
    });

    if args.json {
        println!("{}", serde_json::to_string_pretty(&report)?);
    } else {
        eprintln!("{}", serde_json::to_string_pretty(&report)?);
    }
    Ok(())
}
