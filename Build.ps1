# SPDX-License-Identifier: GPL-3.0-or-later
# Main entry point for preflight, numbered local packages, and release exports.
# Parameters are forwarded unchanged to the maintained build orchestrator.
#Requires -Version 5.1
& (Join-Path $PSScriptRoot 'tools/build/Build-Deployment.ps1') @args
