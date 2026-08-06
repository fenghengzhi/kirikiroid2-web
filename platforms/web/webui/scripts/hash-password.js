#!/usr/bin/env node
// 生成 ADMIN_PASSWORD_HASH。
//
//   node scripts/hash-password.js '你的密码'
//   wrangler secret put ADMIN_PASSWORD_HASH   # 粘贴输出
//
// 复用 worker/auth.js 的 hashPassword，保证生成与校验永远同一套参数。
// Node 18+ 的 globalThis.crypto 就是 WebCrypto，和 Workers runtime 同 API。

import { hashPassword } from '../worker/auth.js';

const password = process.argv[2];

if (!password) {
    console.error('用法: node scripts/hash-password.js \'你的密码\'');
    process.exit(1);
}

if (password.length < 8) {
    console.error('密码至少 8 位。');
    process.exit(1);
}

const hash = await hashPassword(password);

// --raw：只输出哈希本身、不带换行，便于直接管进 wrangler：
//   node scripts/hash-password.js '密码' --raw | wrangler secret put ADMIN_PASSWORD_HASH
if (process.argv.includes('--raw')) {
    process.stdout.write(hash);
    process.exit(0);
}

console.log('\n把下面这行整体作为 ADMIN_PASSWORD_HASH 的值：\n');
console.log(hash);
console.log('\n设置命令（--raw 避免尾部换行混进 secret）：');
console.log(`  node scripts/hash-password.js '你的密码' --raw | npx wrangler secret put ADMIN_PASSWORD_HASH`);
console.log('\n本地开发则写入 .dev.vars（该文件已在 .gitignore 中）：');
console.log(`  ADMIN_PASSWORD_HASH="${hash}"`);
console.log('  SESSION_SECRET="$(openssl rand -base64 32)"\n');
