// Run from repository root; keep measured palette and valid-index data upstream.
const fs = require('fs');
const source = fs.readFileSync('tools/ForFilm/js/convert.js', 'utf8');
let output = '// Generated from tools/ForFilm/js/convert.js, measured HEX palettes.\n';
for (const [upstream, dart] of [['CF_PALETTE_FAST', 'colorFastPalette'], ['CF_PALETTE_QUAL', 'colorQualPalette']]) {
  const match = source.match(new RegExp('var ' + upstream + ' = (\\[[\\s\\S]*?\\n\\]);'));
  if (!match) throw new Error('Missing upstream palette: ' + upstream);
  output += 'const ' + dart + ' = <List<int>>' + match[1] + ';\n';
}
const candidates = [...source.matchAll(/candidateIndexes: (\[[\s\S]*?\])/g)];
if (candidates.length !== 2) throw new Error('Expected exactly two upstream candidate arrays');
output += 'const colorFastCandidates = <int>' + candidates[0][1] + ';\n';
output += 'const colorQualCandidates = <int>' + candidates[1][1] + ';\n';
fs.writeFileSync('flutter_app/lib/frame_palettes.dart', output);
