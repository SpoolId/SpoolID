"""Loads the Creality material_database.json (result.list[].base shape)."""

import dataclasses
import json
from pathlib import Path


@dataclasses.dataclass(frozen=True)
class Material:
    """One filament entry from the material database."""

    id: str
    brand: str
    name: str
    type: str


class MaterialDb:
    """In-memory view of the material database with brand/filament lookups."""

    def __init__(self, items: list[Material]) -> None:
        self._items = items

    @property
    def items(self) -> list[Material]:
        return self._items

    def brands(self) -> list[str]:
        """Return the distinct brand names, sorted."""
        return sorted({m.brand for m in self._items})

    def by_brand(self, brand: str) -> list[Material]:
        """Return the materials for a given brand, in file order."""
        return [m for m in self._items if m.brand == brand]

    @classmethod
    def load(cls, path: str | Path) -> "MaterialDb":
        """Load and parse a material_database.json file.

        Args:
            path: Path to the JSON file.

        Returns:
            A populated MaterialDb.

        Raises:
            KeyError, json.JSONDecodeError: If the file is malformed.
        """
        doc = json.loads(Path(path).read_text(encoding="utf-8"))
        items = []
        for entry in doc["result"]["list"]:
            base = entry.get("base")
            if not base:
                continue
            items.append(
                Material(
                    id=base.get("id", ""),
                    brand=base.get("brand", ""),
                    name=base.get("name", ""),
                    type=base.get("meterialType", ""),
                )
            )
        return cls(items)
