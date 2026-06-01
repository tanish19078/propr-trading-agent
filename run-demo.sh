#!/bin/bash
export PROPR_API_KEY=pk_live_lTTjhW9SxHnsMKnB8ouUcFTje7Z7w6M61GPI3QfpQRdfFfBu
export PATH=/c/msys64/ucrt64/bin:/c/msys64/usr/bin:$PATH
cd "$(dirname "$0")"
exec ./build/app/propr_agent.exe --profile live "$@"
