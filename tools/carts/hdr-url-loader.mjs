// Node import shims for the verbatim Cartridge pipeline copy:
//  - `../assets/studio.hdr?url` is a Vite-style URL import with no meaning under plain Node; resolve it to a tiny
//    module exporting the file's path (the baked geometry never reads it; only the shipped renderer does).
//  - `react/jsx-runtime` is pulled in by Cartridge's shared/logo.js (a JSX wordmark component). The generator never
//    renders it - the owner's engraving choice drops the wordmark, and none/badge/lines never call the component -
//    so a stub with the two factory names and Fragment is enough to let the module load.
import { fileURLToPath } from 'node:url';

export async function resolve(specifier, context, nextResolve) {
  if (specifier === 'react/jsx-runtime') {
    const code = 'export const jsx = () => null;\nexport const jsxs = () => null;\nexport const Fragment = Symbol("Fragment");\nexport default { jsx, jsxs, Fragment };';
    return { url: 'data:text/javascript,' + encodeURIComponent(code), shortCircuit: true, format: 'module' };
  }
  const q = specifier.indexOf('?url');
  if (q < 0) return nextResolve(specifier, context);
  const base = new URL(specifier.slice(0, q), context.parentURL);
  const code = `export default ${JSON.stringify(fileURLToPath(base))};`;
  return { url: 'data:text/javascript,' + encodeURIComponent(code), shortCircuit: true, format: 'module' };
}
