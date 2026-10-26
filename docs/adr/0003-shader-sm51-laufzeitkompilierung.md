# ADR-0003: HLSL Shader Model 5.1, Laufzeit-Kompilierung mit Cache

**Status:** Accepted
**Datum:** 2026-10-26

## Kontext
Ziel ist maximale Hardware-Kompatibilität (Feature Level 11_0+). Die Linux-Testumgebung
(Wine 9 + vkd3d) unterstützt DXBC zuverlässig, DXIL (SM 6.x) jedoch nicht. `d3dcompiler_47.dll`
ist Bestandteil jeder Windows-10/11-Installation.

## Entscheidung
- Shader werden in HLSL für **Shader Model 5.1** geschrieben und zur Laufzeit mit `D3DCompile`
  (FXC) übersetzt; der Bytecode wird unter `%LOCALAPPDATA%/IronTide/shadercache` zwischengespeichert
  (Schlüssel: Hash aus Quelltext, Includes, Defines, Ziel).
- Hot-Reload im Entwicklermodus (F5): geänderte `.hlsl`-Dateien werden neu kompiliert, die
  Pipelines neu erzeugt.
- Der Shader-Lader ist hinter einer Schnittstelle gekapselt, sodass ein DXC/DXIL-Pfad (SM 6.x,
  `dxcompiler.dll`) später ergänzt werden kann.

## Optionen
- **SM 5.1 + FXC zur Laufzeit (gewählt):** überall verfügbar, testbar unter Wine, Hot-Reload.
- **SM 6.x + DXC offline:** modernerer Compiler, Wave-Intrinsics; nicht unter Wine/vkd3d testbar.
- **Beides parallel:** doppelte Pflege ohne aktuellen Feature-Bedarf.

## Konsequenzen
- Kein Build-Schritt für Shader, kein Shader-Compiler in CI nötig.
- Erster Start kompiliert alle Shader (wenige Sekunden), danach aus Cache.
- Kein Zugriff auf SM6-Funktionen (Wave-Ops, 16-Bit-Typen) – für den Umfang nicht erforderlich.
