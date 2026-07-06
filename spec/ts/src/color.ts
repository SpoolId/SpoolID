// Color handling — one convention for every client: colors travel on the wire
// as bare RRGGBB; the '#' is presentation only.

// Strip an optional '#', validate 6 hex chars, return uppercase bare hex (or null).
export function normalizeHex(input: string): string | null {
  const hex = input.trim().replace(/^#/, "");
  return /^[0-9a-fA-F]{6}$/.test(hex) ? hex.toUpperCase() : null;
}

export const withHash = (bareHex: string): string => "#" + bareHex.replace(/^#/, "");

// Friendly names for the Creality swatch palette (presentation only; the hex
// list itself comes from the device spec). Unknown hex -> undefined.
const COLOR_NAMES: Record<string, string> = {
  "1200F6": "Blue",
  "3894E1": "Light Blue",
  "FEFF01": "Yellow",
  "F8D531": "Gold",
  "F38E24": "Orange",
  "52D048": "Green",
  "00FEBE": "Teal",
  "B700F3": "Purple",
  "EE301A": "Red",
  "FA5959": "Coral",
  "FFFFFF": "White",
  "D8D8D8": "Light Gray",
  "4C4C4C": "Dark Gray",
  "782543": "Maroon",
  "000000": "Black",
};

export const colorName = (hex: string): string | undefined =>
  COLOR_NAMES[hex.replace(/^#/, "").toUpperCase()];
