const fs = require('fs');
const path = require('path');

// 命令行参数
const args = process.argv.slice(2);

if (args.length < 2) {
    console.error('Usage: node generate_manifest.js <local_game_folder> <r2_base_url>');
    console.error('Example: node generate_manifest.js ./mygame https://pub-xxx.r2.dev/mygame/');
    process.exit(1);
}

const localFolder = path.resolve(args[0]);
let baseUrl = args[1];
if (!baseUrl.endsWith('/')) {
    baseUrl += '/';
}

if (!fs.existsSync(localFolder)) {
    console.error(`Error: Folder not found -> ${localFolder}`);
    process.exit(1);
}

// 遍历目录并收集文件信息
function scanDirectory(dir, relativePath = '') {
    let filesList = [];
    const entries = fs.readdirSync(dir, { withFileTypes: true });

    for (const entry of entries) {
        const fullPath = path.join(dir, entry.name);
        const itemRelativePath = relativePath ? `${relativePath}/${entry.name}` : entry.name;

        if (entry.isDirectory()) {
            filesList = filesList.concat(scanDirectory(fullPath, itemRelativePath));
        } else {
            // 排除系统生成的隐藏文件、无用文件
            if (entry.name === '.DS_Store' || entry.name === 'Thumbs.db' || entry.name.endsWith('.exe')) {
                continue;
            }
            
            const stats = fs.statSync(fullPath);
            
            // 路径归一化（统一使用 POSIX 正斜杠格式）
            const normalizedName = itemRelativePath.replace(/\\/g, '/');
            
            filesList.push({
                name: normalizedName,
                // encodeURIComponent 确保包含空格和特殊字符的文件名在 URL 中正确传递
                url: baseUrl + normalizedName.split('/').map(encodeURIComponent).join('/'),
                size: stats.size
            });
        }
    }
    
    return filesList;
}

console.log(`Scanning local folder: ${localFolder}`);
const manifest = scanDirectory(localFolder);

const outputPath = path.join(process.cwd(), 'manifest.json');
fs.writeFileSync(outputPath, JSON.stringify(manifest, null, 2), 'utf-8');

console.log(`\n✅ Generated manifest.json successfully!`);
console.log(`Total files: ${manifest.length}`);
console.log(`Output path: ${outputPath}\n`);
console.log(`How to use:`);
console.log(`1. Upload the game files and manifest.json to your R2 bucket at: ${baseUrl}`);
console.log(`2. In the web game gallery admin panel, set the downloadUrl to: ${baseUrl}manifest.json`);
