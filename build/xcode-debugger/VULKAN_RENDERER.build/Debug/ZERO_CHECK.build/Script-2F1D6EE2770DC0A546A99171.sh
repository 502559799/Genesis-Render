#!/bin/sh
set -e
if test "$CONFIGURATION" = "Debug"; then :
  cd /Users/bytedance/.openclaw/workspace/Genesis-Render/build/xcode-debugger
  make -f /Users/bytedance/.openclaw/workspace/Genesis-Render/build/xcode-debugger/CMakeScripts/ReRunCMake.make
fi
if test "$CONFIGURATION" = "Release"; then :
  cd /Users/bytedance/.openclaw/workspace/Genesis-Render/build/xcode-debugger
  make -f /Users/bytedance/.openclaw/workspace/Genesis-Render/build/xcode-debugger/CMakeScripts/ReRunCMake.make
fi
if test "$CONFIGURATION" = "MinSizeRel"; then :
  cd /Users/bytedance/.openclaw/workspace/Genesis-Render/build/xcode-debugger
  make -f /Users/bytedance/.openclaw/workspace/Genesis-Render/build/xcode-debugger/CMakeScripts/ReRunCMake.make
fi
if test "$CONFIGURATION" = "RelWithDebInfo"; then :
  cd /Users/bytedance/.openclaw/workspace/Genesis-Render/build/xcode-debugger
  make -f /Users/bytedance/.openclaw/workspace/Genesis-Render/build/xcode-debugger/CMakeScripts/ReRunCMake.make
fi

