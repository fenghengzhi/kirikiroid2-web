#!/usr/bin/env python3
import os
import sys
import json
import urllib.parse

def generate_manifest(local_folder, base_url):
    local_folder = os.path.abspath(local_folder)
    if not base_url.endswith('/'):
        base_url += '/'

    if not os.path.exists(local_folder) or not os.path.isdir(local_folder):
        print(f"Error: Directory not found -> {local_folder}")
        sys.exit(1)

    files_list = []

    # 遍历目录
    for root, _, files in os.walk(local_folder):
        for file in files:
            # 排除系统级隐藏文件或无用文件
            if file in ['.DS_Store', 'Thumbs.db'] or file.endswith('.exe'):
                continue

            full_path = os.path.join(root, file)
            # 计算相对于本地根目录的相对路径
            relative_path = os.path.relpath(full_path, local_folder)
            # 将 Windows 风格的路径分隔符替换为 POSIX 正斜杠
            normalized_name = relative_path.replace(os.sep, '/')
            
            # 使用 urllib.parse.quote 对路径中的每一部分进行 URL 编码（确保空格和特殊字符不越界）
            url_parts = [urllib.parse.quote(part) for part in normalized_name.split('/')]
            final_url = base_url + '/'.join(url_parts)
            
            # 获取文件大小
            size = os.path.getsize(full_path)
            
            files_list.append({
                "name": normalized_name,
                "url": final_url,
                "size": size
            })

    return files_list

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print("Usage: python generate_manifest.py <local_game_folder> <r2_base_url>")
        print("Example: python generate_manifest.py ./mygame https://pub-xxx.r2.dev/mygame/")
        sys.exit(1)

    local_game_folder = sys.argv[1]
    r2_base_url = sys.argv[2]

    print(f"Scanning local folder: {local_game_folder}")
    manifest_data = generate_manifest(local_game_folder, r2_base_url)

    output_path = os.path.join(os.getcwd(), 'manifest.json')
    with open(output_path, 'w', encoding='utf-8') as f:
        json.dump(manifest_data, f, indent=2, ensure_ascii=False)

    print("\n✅ Generated manifest.json successfully!")
    print(f"Total files: {len(manifest_data)}")
    print(f"Output path: {output_path}\n")
    print("How to use:")
    if not r2_base_url.endswith('/'):
        r2_base_url += '/'
    print(f"1. Upload the game files and manifest.json to your R2 bucket at: {r2_base_url}")
    print(f"2. In the web game gallery admin panel, set the downloadUrl to: {r2_base_url}manifest.json")
