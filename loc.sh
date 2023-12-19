#!/bin/bash

# approx 52.000 lines of code

find . \( -name \*.in -or -name \*.cxx -or -name \*.h -or -name \*.cmake -or -name \*.cpp \) ! -path "./ThirdParty/*" ! -path "./build/*" ! -path "./build_Debug/*" ! -path "./build_debug/*" ! -path "./TTK/*" | xargs -i -t less {} 2>/dev/null | wc -l | numfmt --to=iec-i
