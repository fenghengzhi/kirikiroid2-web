import { readFile, writeFile } from 'node:fs/promises';

const args = process.argv.slice(2);
const checkOnly = args.includes('--check');
const paths = args.filter((arg) => arg !== '--check');

if (paths.length !== 1) {
  console.error(
      'usage: node platforms/web/patch_logo_chain_trace.mjs [--check] <index.js>');
  process.exit(2);
}

const file = paths[0];
const source = await readFile(file, 'utf8');
const marker = /__KRKR_TRACE_LOGO_CHAIN__/g;
const query = /(\d+):\(\)=>\{try\{if\(typeof window!==["']undefined["']&&window\.__KRKR_TRACE_LOGO_CHAIN__\)\{return 1\}[\s\S]*?\}catch\(e\)\{return 0\}\}/g;

const markerCount = Array.from(source.matchAll(marker)).length;
let patchedCount = 0;
const patched = source.replace(query, (_match, address) => {
  patchedCount += 1;
  return `${address}:()=>1`;
});

if (markerCount === 0 || patchedCount !== markerCount) {
  console.error(
      `logo-chain glue shape mismatch: markers=${markerCount}, queries=${patchedCount}`);
  process.exit(1);
}

if (!checkOnly) {
  await writeFile(file, patched);
}

console.log(
    `${checkOnly ? 'validated' : 'patched'} ${patchedCount} logo-chain queries in ${file}`);
