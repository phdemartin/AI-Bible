#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST_DIR="$PROJECT_DIR/dist"
CACHE_DIR="$PROJECT_DIR/build/win64_cache"
mkdir -p "$DIST_DIR" "$CACHE_DIR/dnf" "$CACHE_DIR/work"

HOST_UID="$(id -u)"
HOST_GID="$(id -g)"

echo "=== Cross-Compilation Windows 64 bits (AI_Bible.exe + DLLs) via conteneur Fedora MinGW-w64 ==="

docker run --rm \
    --network=host \
    -v "$PROJECT_DIR:/workspace" \
    -v "$CACHE_DIR/dnf:/var/cache/dnf" \
    -v "$CACHE_DIR/work:/tmp/win64_work" \
    -e HOST_UID="$HOST_UID" \
    -e HOST_GID="$HOST_GID" \
    fedora:41 bash -c '
set -euo pipefail

echo "-> 1. Installation de la chaîne de compilation MinGW-w64 (Qt6, libcurl, ICU, zlib)..."
dnf install -y -q --setopt=keepcache=1 \
    mingw64-gcc-c++ \
    mingw64-cmake \
    mingw64-pkg-config \
    mingw64-qt6-qtbase \
    mingw64-qt6-qtsvg \
    mingw64-curl \
    mingw64-zlib \
    mingw64-bzip2 \
    mingw64-xz-libs \
    mingw64-icu \
    qt6-qtbase-devel \
    cmake \
    make \
    ninja-build \
    curl \
    tar \
    zip \
    binutils \
    ca-certificates

MINGW_PREFIX="/usr/x86_64-w64-mingw32/sys-root/mingw"

echo "-> 2. Téléchargement et compilation de libsword 1.9.0 pour Windows x86_64..."
mkdir -p /tmp/win64_work/sword-build
cd /tmp/win64_work/sword-build
if [ ! -f sword-1.9.0.tar.gz ]; then
    curl -fsSL "https://crosswire.org/ftpmirror/pub/sword/source/v1.9/sword-1.9.0.tar.gz" -o sword-1.9.0.tar.gz
fi
if [ ! -d sword-1.9.0 ]; then
    tar -xzf sword-1.9.0.tar.gz
fi
cd sword-1.9.0

# Assurer la compatibilité CMake moderne et MinGW-w64
sed -i "s/cmake_minimum_required(VERSION 2.6.0)/cmake_minimum_required(VERSION 3.5)/g" CMakeLists.txt || true

mkdir -p build-win64 && cd build-win64
mingw64-cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DLIBSWORD_LIBRARY_TYPE=Shared \
    -DSWORD_BUILD_TESTS=No \
    -DSWORD_BUILD_EXAMPLES=No \
    -DSWORD_BUILD_UTILS=No \
    -DCMAKE_C_FLAGS="-DSTDC_HEADERS=1 -Wno-error=implicit-function-declaration -Wno-implicit-function-declaration" \
    -DCMAKE_CXX_FLAGS="-fpermissive -Wno-narrowing -Wno-deprecated-declarations -include cstdint"
make -j$(nproc)
make install

# Vérifier ou générer sword.pc pour pkg-config sous MinGW
if [ ! -f "$MINGW_PREFIX/lib/pkgconfig/sword.pc" ]; then
    mkdir -p "$MINGW_PREFIX/lib/pkgconfig"
    cat > "$MINGW_PREFIX/lib/pkgconfig/sword.pc" << EOF
prefix=$MINGW_PREFIX
exec_prefix=\${prefix}
libdir=\${exec_prefix}/lib
includedir=\${prefix}/include

Name: sword
Description: Biblical Text Research Engine
Version: 1.9.0
Libs: -L\${libdir} -lsword -lcurl -lz
Cflags: -I\${includedir}/sword -I\${includedir}
EOF
fi

echo "-> 3. Compilation de AI_Bible.exe (C++20 / Qt6 / SWORD)..."
mkdir -p /tmp/win64_work/aibible-win64
cd /tmp/win64_work/aibible-win64
mingw64-cmake /workspace \
    -DCMAKE_BUILD_TYPE=Release \
    -DQT_HOST_PATH=/usr
make -j$(nproc)

echo "-> 4. Assemblage du dossier portable Windows (AI_Bible-1.0.0-Windows-x64)..."
PKG_DIR="/tmp/AI_Bible-1.0.0-Windows-x64"
rm -rf "$PKG_DIR"
mkdir -p "$PKG_DIR"

EXE_PATH="$(find /tmp/win64_work/aibible-win64 -name "AI_Bible.exe" | head -n 1)"
cp "$EXE_PATH" "$PKG_DIR/AI_Bible.exe"
x86_64-w64-mingw32-strip --strip-unneeded "$PKG_DIR/AI_Bible.exe"

# Copier les plugins Qt6 nécessaires pour Windows
QT_PLUGINS="$MINGW_PREFIX/lib/qt6/plugins"
for pdir in platforms styles tls iconengines imageformats; do
    if [ -d "$QT_PLUGINS/$pdir" ]; then
        cp -r "$QT_PLUGINS/$pdir" "$PKG_DIR/"
    fi
done

# Fonction récursive pour copier toutes les DLLs MinGW requises par AI_Bible.exe et les plugins Qt6
copy_dll_deps() {
    local target="$1"
    x86_64-w64-mingw32-objdump -p "$target" 2>/dev/null | awk "/DLL Name:/ { print \$3 }" | while read -r dll; do
        if [ -f "$MINGW_PREFIX/bin/$dll" ] && [ ! -f "$PKG_DIR/$dll" ]; then
            cp -L "$MINGW_PREFIX/bin/$dll" "$PKG_DIR/$dll"
            copy_dll_deps "$PKG_DIR/$dll"
        fi
    done
}

copy_dll_deps "$PKG_DIR/AI_Bible.exe"
find "$PKG_DIR" -name "*.dll" | while read -r plugin_dll; do
    copy_dll_deps "$plugin_dll"
done
# Deuxième passe pour être certain de n oublier aucune sous-dépendance
find "$PKG_DIR" -maxdepth 1 -name "*.dll" | while read -r top_dll; do
    copy_dll_deps "$top_dll"
done

# Inclure l icône, le certificat CA (pour libcurl/SWORD), et un guide LISEZMOI
cp /workspace/ai_bible_icon.svg "$PKG_DIR/ai_bible_icon.svg"
if [ -f /etc/pki/tls/certs/ca-bundle.crt ]; then
    cp -L /etc/pki/tls/certs/ca-bundle.crt "$PKG_DIR/ca-bundle.crt"
fi
mkdir -p "$PKG_DIR/scripts"
cp /workspace/scripts/live_dictation.py "$PKG_DIR/scripts/live_dictation.py"

cat > "$PKG_DIR/LISEZMOI_WINDOWS.txt" << "EOF"
======================================================================
   AI Bible — Logos & Lumiere (Version Portable Windows 64 bits)
======================================================================

1. LANCEMENT :
   Double-cliquez simplement sur "AI_Bible.exe" (aucune installation
   compliquee requise, toutes les bibliotheques Qt6 et SWORD sont incluses).

2. PREMIER DEMARRAGE & MISE EN PAGE :
   - Choisissez votre configuration (1 Ecran ou 2 Ecrans) et votre
     disposition de colonnes (2 Colonnes, 3 Fenetres Mixtes, 3 Colonnes
     ou 4 Colonnes Studio).
   - Vous pouvez changer de disposition a tout moment via le bouton
     "Mise en page" en haut de la fenetre.

3. TELECHARGEMENT DES BIBLES, DICTIONNAIRES ET COMMENTAIRES :
   - Cliquez sur le bouton "Telecharger / Gerer des Modules" (en haut
     a gauche ou dans la fenetre d accueil).
   - Cochez les langues souhaitees (Francais, Espanol, English, Grec,
     Hebreu, etc.), cliquez sur "Actualiser les catalogues Internet",
     puis installez librement vos Bibles et outils en 1 clic !
   - Les modules sont stockes dans votre dossier personnel :
     C:\Users\<VotreNom>\.sword
======================================================================
EOF

cd /tmp
rm -f /workspace/dist/AI_Bible-1.0.0-Windows-x64.zip
zip -r -q /workspace/dist/AI_Bible-1.0.0-Windows-x64.zip AI_Bible-1.0.0-Windows-x64
chown "$HOST_UID:$HOST_GID" /workspace/dist/AI_Bible-1.0.0-Windows-x64.zip
'

echo "=== Paquet Windows créé avec succès : $DIST_DIR/AI_Bible-1.0.0-Windows-x64.zip ==="
ls -lh "$DIST_DIR"
