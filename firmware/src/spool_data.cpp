#include "spool_data.h"

namespace spool {

String weightCode(const String &sizeLabel) {
  if (sizeLabel == "1KG")  return "0330";
  if (sizeLabel == "750G") return "0247";
  if (sizeLabel == "600G") return "0198";
  if (sizeLabel == "500G") return "0165";
  if (sizeLabel == "250G") return "0082";
  return "";
}

String sizeLabel(const String &lengthCode) {
  if (lengthCode == "0330") return "1KG";
  if (lengthCode == "0247") return "750G";
  if (lengthCode == "0198") return "600G";
  if (lengthCode == "0165") return "500G";
  if (lengthCode == "0082") return "250G";
  return "";
}

Parsed parse(const String &data48) {
  Parsed p;
  if (data48.length() != 48) return p;
  // Layout: "AB124"(5)+vendor(4)+"A2"(2)+filamentId(6)+color(7)+len(4)+serial(6)+...
  p.materialId = data48.substring(12, 17);   // skip filamentId prefix '1'
  p.color = data48.substring(18, 24);         // skip color prefix '0'
  p.weight = sizeLabel(data48.substring(24, 28));
  p.serial = data48.substring(28, 34);
  p.color.toUpperCase();
  p.ok = true;
  return p;
}

String build(const String &materialId, const String &rgb, const String &sizeLabel, const String &serial6) {
  if (materialId.length() != 5) return "";

  String len = weightCode(sizeLabel);
  if (len.isEmpty()) return "";

  String color = rgb;
  color.trim();
  if (color.startsWith("#")) color = color.substring(1);
  if (color.length() != 6) return "";
  color.toUpperCase();
  for (size_t i = 0; i < 6; i++) {
    char c = color[i];
    bool hex = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
    if (!hex) return "";
  }

  String serial = serial6;
  if (serial.length() != 6) {
    serial = "";
    for (int i = 0; i < 6; i++) serial += char('0' + random(10));
  }

  String filamentId = "1" + materialId;  // 6 chars

  String out;
  out.reserve(48);
  out += "AB124";            // 5
  out += VENDOR_CREALITY;    // 4
  out += "A2";               // 2
  out += filamentId;         // 6
  out += "0";                // color prefix
  out += color;              // 6  -> color field = 7
  out += len;                // 4
  out += serial;             // 6
  out += "000000";           // 6 reserve
  out += "00000000";         // 8
  return out.length() == 48 ? out : String("");
}

}  // namespace spool
