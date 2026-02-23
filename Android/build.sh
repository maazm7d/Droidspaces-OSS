#!/bin/bash
# Build script for Droidspaces APK
# Usage: ./build.sh [debug|release]
# Default: debug

set -e

# Parse build type argument
BUILD_TYPE="${1:-debug}"

# Validate build type
if [ "$BUILD_TYPE" != "debug" ] && [ "$BUILD_TYPE" != "release" ]; then
    echo "❌ Invalid build type: $BUILD_TYPE"
    echo "Usage: ./build.sh [debug|release]"
    exit 1
fi

echo "🔨 Building Droidspaces APK ($BUILD_TYPE)..."
echo ""

cd "$(dirname "$0")"

# Check if gradlew exists, if not create it
if [ ! -f "gradlew" ]; then
    echo "📦 Setting up Gradle wrapper..."
    gradle wrapper --gradle-version 8.2
fi

# Make gradlew executable
chmod +x gradlew

# Clean previous builds
echo "🧹 Cleaning previous builds..."
./gradlew clean

# Build APK based on type
if [ "$BUILD_TYPE" = "release" ]; then
    echo "🔨 Building release APK..."
    ./gradlew assembleRelease

    APK_PATH="app/build/outputs/apk/release/app-release.apk"
    INSTALL_CMD="./gradlew installRelease"
else
    echo "🔨 Building debug APK..."
    ./gradlew assembleDebug

    APK_PATH="app/build/outputs/apk/debug/app-debug.apk"
    INSTALL_CMD="./gradlew installDebug"
fi

# Check if build was successful
if [ -f "$APK_PATH" ]; then
    APK_SIZE=$(du -h "$APK_PATH" | cut -f1)
    echo ""
    echo "✅ Build successful!"
    echo "📦 APK location: $APK_PATH"
    echo "📊 APK size: $APK_SIZE"
    echo ""
    echo "📱 To install on connected device:"
    echo "   $INSTALL_CMD"
else
    echo "❌ Build failed! Check errors above."
    exit 1
fi

