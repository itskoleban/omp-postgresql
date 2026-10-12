param(
	[Parameter(Mandatory = $true)]
	[string]$PostgreSQLRoot,
	[Parameter(Mandatory = $true)]
	[string]$LibTool
)

$ErrorActionPreference = 'Stop'
$pgRoot = (Resolve-Path -LiteralPath $PostgreSQLRoot).Path
$pgDefinition = Join-Path $pgRoot 'lib/libpq.def'

if (-not (Test-Path -LiteralPath $pgDefinition -PathType Leaf)) {
	throw 'Missing libpq.def; build the client first.'
}

$pgImport = Join-Path $pgRoot 'lib/libpq.lib'

& $LibTool /nologo "/def:$pgDefinition" /machine:x86 "/out:$pgImport"
if ($LASTEXITCODE -ne 0) {
	throw 'MSVC import library generation failed.'
}
