function lcarsRelativeLuminance(hex: string): number {
  if (!/^#[0-9a-f]{6}$/i.test(hex)) {
    throw new Error(`LCARS contrast requires six-digit hex colors: ${hex}`);
  }
  const channels = [1, 3, 5].map((start) => {
    const channel = parseInt(hex.slice(start, start + 2), 16) / 255;
    return channel <= 0.04045
      ? channel / 12.92
      : ((channel + 0.055) / 1.055) ** 2.4;
  });
  return channels[0]! * 0.2126 + channels[1]! * 0.7152 + channels[2]! * 0.0722;
}

export function getLcarsContrastRatio(
  foreground: string,
  background: string,
): number {
  const first = lcarsRelativeLuminance(foreground);
  const second = lcarsRelativeLuminance(background);
  return (Math.max(first, second) + 0.05) / (Math.min(first, second) + 0.05);
}

export function getLcarsForegroundColor(
  background: string,
): "#000000" | "#FFFFFF" {
  return getLcarsContrastRatio("#000000", background) >=
    getLcarsContrastRatio("#FFFFFF", background)
    ? "#000000"
    : "#FFFFFF";
}
