#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build"
DIST_DIR="$PROJECT_DIR/dist"
STAGE_DIR="$BUILD_DIR/packaging_linux"

mkdir -p "$DIST_DIR"
rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR"

echo "=== 1. Création du paquet Debian (.deb) ultra-léger ==="
DEB_ROOT="$STAGE_DIR/ai-bible_1.0.0_amd64"
mkdir -p "$DEB_ROOT/DEBIAN"
mkdir -p "$DEB_ROOT/usr/bin"
mkdir -p "$DEB_ROOT/usr/share/applications"
mkdir -p "$DEB_ROOT/usr/share/icons/hicolor/scalable/apps"
mkdir -p "$DEB_ROOT/usr/share/ai-bible/scripts"

cp "$BUILD_DIR/bible_study_studio" "$DEB_ROOT/usr/bin/bible_study_studio"
strip --strip-unneeded "$DEB_ROOT/usr/bin/bible_study_studio"
chmod 755 "$DEB_ROOT/usr/bin/bible_study_studio"

cp "$PROJECT_DIR/ai_bible_icon.svg" "$DEB_ROOT/usr/share/icons/hicolor/scalable/apps/ai_bible_icon.svg"
cp "$PROJECT_DIR/scripts/live_dictation.py" "$DEB_ROOT/usr/share/ai-bible/scripts/live_dictation.py"
chmod 755 "$DEB_ROOT/usr/share/ai-bible/scripts/live_dictation.py"

cat > "$DEB_ROOT/usr/share/applications/ai-bible.desktop" << 'EOF'
[Desktop Entry]
Type=Application
Name=AI Bible — Logos & Lumière
GenericName=Studio d'Étude Biblique & IA
Comment=Étude biblique multi-colonnes, téléchargeur SWORD, Grec/Hébreu Strong, Notes & IA Gemini
Exec=/usr/bin/bible_study_studio
Icon=ai_bible_icon
Terminal=false
Categories=Education;Literature;Qt;
StartupWMClass=BibleStudyStudioMainWindow
EOF

cat > "$DEB_ROOT/DEBIAN/control" << 'EOF'
Package: ai-bible
Version: 1.0.0
Section: education
Priority: optional
Architecture: amd64
Depends: libsword1v5 | libsword-common, libqt6widgets6, libqt6gui6, libqt6network6, libqt6core6t64 | libqt6core6
Recommends: python3, ffmpeg
Maintainer: Logos & Lumiere <contact@logoslumiere.org>
Description: AI Bible - Logos & Lumiere (Studio Biblique Exegetique & IA)
 Studio d'etude biblique multi-colonnes (1 ou 2 ecrans) avec telechargeur
 de modules SWORD integre (CrossWire, eBible, STEP Bible, Xiphos), lexiques
 Grec/Hebreu Strong, commentaires, fiches de notes personnelles indexees
 par verset et assistant exegétique IA Gemini (Francais / Espagnol / Anglais).
EOF

dpkg-deb --build --root-owner-group "$DEB_ROOT" "$DIST_DIR/ai-bible_1.0.0_amd64.deb"
echo "-> Paquet .deb créé : $DIST_DIR/ai-bible_1.0.0_amd64.deb"

echo "=== 2. Création de l'AppImage autonome (AI_Bible-1.0.0-x86_64.AppImage) ==="
APPDIR="$STAGE_DIR/AppDir"
mkdir -p "$APPDIR/usr/bin"
mkdir -p "$APPDIR/usr/lib"
mkdir -p "$APPDIR/usr/plugins"
mkdir -p "$APPDIR/usr/share/applications"
mkdir -p "$APPDIR/usr/share/icons/hicolor/scalable/apps"
mkdir -p "$APPDIR/usr/share/ai-bible/scripts"

cp "$DEB_ROOT/usr/bin/bible_study_studio" "$APPDIR/usr/bin/bible_study_studio"
cp "$PROJECT_DIR/ai_bible_icon.svg" "$APPDIR/ai_bible_icon.svg"
ln -sf "ai_bible_icon.svg" "$APPDIR/.DirIcon"
cp "$PROJECT_DIR/ai_bible_icon.svg" "$APPDIR/usr/share/icons/hicolor/scalable/apps/ai_bible_icon.svg"
cp "$PROJECT_DIR/scripts/live_dictation.py" "$APPDIR/usr/share/ai-bible/scripts/live_dictation.py"

cat > "$APPDIR/ai-bible.desktop" << 'EOF'
[Desktop Entry]
Type=Application
Name=AI Bible — Logos & Lumière
GenericName=Studio d'Étude Biblique & IA
Comment=Étude biblique multi-colonnes, téléchargeur SWORD, Grec/Hébreu Strong, Notes & IA Gemini
Exec=bible_study_studio
Icon=ai_bible_icon
Terminal=false
Categories=Education;Literature;Qt;
StartupWMClass=BibleStudyStudioMainWindow
EOF
cp "$APPDIR/ai-bible.desktop" "$APPDIR/usr/share/applications/ai-bible.desktop"

# Copier les plugins Qt6 nécessaires (platforms, imageformats, iconengines, tls, xcbglintegrations)
QT_PLUGIN_SRC="/usr/lib/x86_64-linux-gnu/qt6/plugins"
for plug_dir in platforms imageformats iconengines tls xcbglintegrations wayland-shell-integration wayland-decoration-client; do
    if [ -d "$QT_PLUGIN_SRC/$plug_dir" ]; then
        cp -r "$QT_PLUGIN_SRC/$plug_dir" "$APPDIR/usr/plugins/"
    fi
done

# Fonction pour déterminer si une bibliothèque système de bas niveau doit être exclue de l'AppImage
should_exclude_lib() {
    local base
    base="$(basename "$1")"
    case "$base" in
        ld-linux*.so*|libc.so*|libm.so*|libdl.so*|librt.so*|libpthread.so*|libresolv.so*|libutil.so*|libnsl.so*|libanl.so*|libBrokenLocale.so*)
            return 0 ;;
        libGL.so*|libGLX.so*|libOpenGL.so*|libGLdispatch.so*|libEGL.so*|libgbm.so*|libdrm*.so*)
            return 0 ;;
        libX11.so*|libX11-xcb.so*|libxcb.so*|libXau.so*|libXdmcp.so*|libXext.so*)
            return 0 ;;
        libwayland-*.so*|libdbus-1.so*|libsystemd.so*|libmount.so*|libblkid.so*|libselinux.so*|libudev.so*)
            return 0 ;;
        libfontconfig.so*|libfreetype.so*|libharfbuzz.so*)
            return 0 ;;
        *)
            return 1 ;;
    esac
}

echo "-> Collecte des bibliothèques partagées (libsword, Qt6, curl, clucene, icu)..."
collect_deps() {
    local target="$1"
    ldd "$target" 2>/dev/null | awk '/=> \// { print $3 }' | while read -r libpath; do
        if [ -f "$libpath" ] && ! should_exclude_lib "$libpath"; then
            local libname
            libname="$(basename "$libpath")"
            if [ ! -f "$APPDIR/usr/lib/$libname" ]; then
                cp -L "$libpath" "$APPDIR/usr/lib/$libname"
                chmod 755 "$APPDIR/usr/lib/$libname"
            fi
        fi
    done
}

collect_deps "$APPDIR/usr/bin/bible_study_studio"
find "$APPDIR/usr/plugins" -name "*.so" | while read -r plugin_so; do
    collect_deps "$plugin_so"
done
# Deuxième passe sur les bibliothèques copiées
find "$APPDIR/usr/lib" -name "*.so*" | while read -r copied_so; do
    collect_deps "$copied_so"
done

# Créer le script de lancement AppRun (compatible avec le téléchargement HTTPS/FTP dans ~/.sword)
cat > "$APPDIR/AppRun" << 'EOF'
#!/usr/bin/env bash
HERE="$(dirname "$(readlink -f "${0}")")"

export PATH="$HERE/usr/bin:$PATH"
export LD_LIBRARY_PATH="$HERE/usr/lib:${LD_LIBRARY_PATH:-}"
export QT_PLUGIN_PATH="$HERE/usr/plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="$HERE/usr/plugins/platforms"

# Garantir que libcurl / QtNetwork trouvent les certificats SSL du système hôte
# pour le téléchargement des modules SWORD (CrossWire, eBible, STEP Bible, Xiphos) et l'API Gemini
for cert_file in \
    "/etc/ssl/certs/ca-certificates.crt" \
    "/etc/pki/tls/certs/ca-bundle.crt" \
    "/etc/ssl/ca-bundle.pem" \
    "/etc/ssl/cert.pem" \
    "/var/lib/ca-certificates/ca-bundle.pem"; do
    if [ -f "$cert_file" ]; then
        export SSL_CERT_FILE="${SSL_CERT_FILE:-$cert_file}"
        export CURL_CA_BUNDLE="${CURL_CA_BUNDLE:-$cert_file}"
        break
    fi
done

# S'assurer que le dossier standard ~/.sword existe pour le partage avec BibleTime et le téléchargement
mkdir -p "$HOME/.sword/mods.d" "$HOME/.sword/modules" "$HOME/.sword/InstallMgr"

exec "$HERE/usr/bin/bible_study_studio" "$@"
EOF
chmod 755 "$APPDIR/AppRun"

# Télécharger le runtime officiel AppImage Type 2 (x86_64) et assembler l'AppImage avec mksquashfs
RUNTIME_BIN="$STAGE_DIR/runtime-x86_64"
curl -fsSL "https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64" -o "$RUNTIME_BIN"
chmod 755 "$RUNTIME_BIN"

SQUASHFS_IMG="$STAGE_DIR/appimage.squashfs"
mksquashfs "$APPDIR" "$SQUASHFS_IMG" -root-owned -noappend -comp zstd -Xcompression-level 15 -quiet

APPIMAGE_OUT="$DIST_DIR/AI_Bible-1.0.0-x86_64.AppImage"
cat "$RUNTIME_BIN" "$SQUASHFS_IMG" > "$APPIMAGE_OUT"
chmod 755 "$APPIMAGE_OUT"

echo "-> AppImage créée avec succès : $APPIMAGE_OUT"
ls -lh "$DIST_DIR"
