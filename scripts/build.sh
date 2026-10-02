#!/bin/bash
# BFM 中文美化版 - 统一构建脚本
# 被 build-x64.yml 和 release.yml 共同调用，消除重复代码
set -euxo pipefail

echo "=== [1/4] 安装编译工具链 ==="
sudo apt-get update
sudo apt-get install -y gcc-mingw-w64-x86-64 binutils-mingw-w64-x86-64 p7zip-full

echo ""
echo "=== [2/4] 编译 wfm.exe (x86-64) ==="
CC=x86_64-w64-mingw32-gcc
RC=x86_64-w64-mingw32-windres
INCLUDES="-I./include -I./include/libcdio"
CFLAGS="-O2 -std=c99 -DUNICODE -D_UNICODE -DCOBJMACROS -DWINVER=0x0600 -Wall"
SRCS="main theme config favorites diff content_view toolbar navbar treeview sizebar statusbar file_node file_actions input_dialog"

mkdir -p obj
for f in $SRCS; do
  $CC $CFLAGS $INCLUDES -c "src/$f.c" -o "obj/$f.o"
done

$RC $INCLUDES -I./res -i res/resource.rc -o obj/resource.o

$CC -o wfm.exe obj/*.o -s -lcomctl32 -lgdi32 -lole32 -loleaut32 -luuid -luxtheme -lshlwapi -lcrypt32 -lmsimg32 ./libcdio.dll -Wl,--subsystem,windows

file wfm.exe
ls -l wfm.exe libcdio.dll

echo ""
echo "=== [3/4] 下载并解压 7-Zip（官方便携版）==="
# 注意：管道后必须加 || true，否则 set -o pipefail 会在 API 限流时直接退出脚本
TAG=$(curl -s --max-time 20 https://api.github.com/repos/ip7z/7zip/releases/latest 2>/dev/null | grep '"tag_name"' | head -1 | sed 's/.*: "\(.*\)".*/\1/' || true)
if [ -z "$TAG" ]; then
  TAG="26.03"
  echo "⚠️  API 获取失败或限流，回退到默认版本 ${TAG}"
fi
VER=$(echo "$TAG" | tr -d '.')
URL="https://github.com/ip7z/7zip/releases/download/${TAG}/7z${VER}-x64.exe"
echo "下载 7-Zip ${TAG}: $URL"
curl -fL --retry 3 --max-time 120 -o "/tmp/7zsetup.exe" "$URL"

mkdir -p /tmp/7z-extract
7z x /tmp/7zsetup.exe -o/tmp/7z-extract -y
rm -rf /tmp/7z-extract/\$PLUGINSDIR 2>/dev/null || true
echo "=== 7-Zip 文件列表 ==="
ls -la /tmp/7z-extract/

echo ""
echo "=== [4/4] 打包发布 zip ==="
OUTPUT_DIR="${1:-pkg}"
mkdir -p "${OUTPUT_DIR}"
cp wfm.exe libcdio.dll "${OUTPUT_DIR}/"
cp -r /tmp/7z-extract "${OUTPUT_DIR}/7-Zip/"
if [ -f 安装.bat ]; then cp 安装.bat "${OUTPUT_DIR}/"; fi
if [ -f 卸载还原.bat ]; then cp 卸载还原.bat "${OUTPUT_DIR}/"; fi

ZIP_NAME="${2:-bfm-zh-x64.zip}"
cd "${OUTPUT_DIR}" && zip -r "../${ZIP_NAME}" . && cd ..
echo "=== zip 内容 ==="
unzip -l "${ZIP_NAME}"
echo ""
echo "✅ 构建完成: ${ZIP_NAME}"
