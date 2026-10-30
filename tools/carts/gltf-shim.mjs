// three's GLTFExporter builds its binary chunk through Blob + FileReader; Node has Blob but no FileReader.
// This shim provides the two read methods the exporter uses, and nothing else.
if (typeof globalThis.FileReader === 'undefined') {
  globalThis.FileReader = class FileReader {
    constructor() {
      this.result = null;
      this.onloadend = null;
      this.onerror = null;
    }

    readAsArrayBuffer(blob) {
      blob.arrayBuffer().then(
        (buf) => {
          this.result = buf;
          this.onloadend?.({ target: this });
        },
        (err) => {
          this.onerror?.(err);
          this.onloadend?.({ target: this });
        },
      );
    }

    readAsDataURL(blob) {
      blob.arrayBuffer().then(
        (buf) => {
          this.result = `data:${blob.type || 'application/octet-stream'};base64,${Buffer.from(buf).toString('base64')}`;
          this.onloadend?.({ target: this });
        },
        (err) => {
          this.onerror?.(err);
          this.onloadend?.({ target: this });
        },
      );
    }
  };
}
