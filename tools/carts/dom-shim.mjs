// @xmldom/xmldom 0.9.8 has no querySelectorAll, and three's SVGLoader asks for two tag-name selectors
// ('linearGradient, radialGradient' and 'stop'). Without this shim the SVG parse throws inside Cartridge's
// buildEngravingGeometry try/catch{} and the engraving silently vanishes - the 4a review's blocking finding.
// This installs querySelectorAll/querySelector on the parsed document's own prototypes (xmldom does not export
// its classes), supporting comma-separated tag names only; anything else throws loudly.
export function installDomShim({ DOMParser }) {
  const probe = new DOMParser().parseFromString(
    '<svg xmlns="http://www.w3.org/2000/svg"><g/></svg>', 'image/svg+xml');
  const protos = new Set([Object.getPrototypeOf(probe), Object.getPrototypeOf(probe.documentElement)]);
  const query = function querySelectorAll(selector) {
    const names = String(selector).split(',').map((s) => s.trim()).filter(Boolean);
    if (names.length === 0) throw new Error(`dom-shim: empty selector "${selector}"`);
    for (const name of names) {
      if (!/^[A-Za-z_][\w:.-]*$/.test(name)) {
        throw new Error(`dom-shim: unsupported selector "${selector}" (tag names only)`);
      }
    }
    const out = [];
    for (const name of names) out.push(...this.getElementsByTagName(name));
    return [...new Set(out)];
  };
  for (const proto of protos) {
    if (typeof proto.querySelectorAll === 'function') continue;
    Object.defineProperty(proto, 'querySelectorAll', { value: query, configurable: true, writable: true });
    Object.defineProperty(proto, 'querySelector', {
      value: function querySelector(selector) { return query.call(this, selector)[0] ?? null; },
      configurable: true, writable: true,
    });
  }
}
