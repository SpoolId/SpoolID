// Pick the OTA target from an image file name or URL.
export const isFilesystemImage = (nameOrUrl: string): boolean =>
  /littlefs|spiffs|filesystem/.test(nameOrUrl.toLowerCase());
