#!/bin/sh
set -e
if test "$CONFIGURATION" = "Debug"; then :
  cd /Users/bytedance/.openclaw/workspace/Genesis-Render/xcode-build
  make -f /Users/bytedance/.openclaw/workspace/Genesis-Render/xcode-build/CMakeScripts/ReRunCMake.make
fi
if test "$CONFIGURATION" = "Release"; then :
  cd /Users/bytedance/.openclaw/workspace/Genesis-Render/xcode-build
  make -f /Users/bytedance/.openclaw/workspace/Genesis-Render/xcode-build/CMakeScripts/ReRunCMake.make
fi
if test "$CONFIGURATION" = "MinSizeRel"; then :
  cd /Users/bytedance/.openclaw/workspace/Genesis-Render/xcode-build
  make -f /Users/bytedance/.openclaw/workspace/Genesis-Render/xcode-build/CMakeScripts/ReRunCMake.make
fi
if test "$CONFIGURATION" = "RelWithDebInfo"; then :
  cd /Users/bytedance/.openclaw/workspace/Genesis-Render/xcode-build
  make -f /Users/bytedance/.openclaw/workspace/Genesis-Render/xcode-build/CMakeScripts/ReRunCMake.make
fi

