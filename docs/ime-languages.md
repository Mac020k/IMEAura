# IME / input-language catalog

Candidate languages for IME Aura color mapping and (separately) UI localization.
**Phase 1 implements Must + Latin fallback (`en`) only.** Higher tiers are documented for future work.

## Phase 1 (implemented)

| ID | Language | Role |
| --- | --- | --- |
| `ja` | Japanese | Must — true IME + native/Latin mode |
| `zh-Hans` | Chinese (Simplified) | Must — true IME + 中/英 |
| `zh-Hant` | Chinese (Traditional) | Must — true IME + 中/英 (TW/HK) |
| `ko` | Korean | Must — true IME + 가/A |
| `en` | English / Latin / other | Fallback when active input is not a Must ID |

Aura settings allow up to **7** color slots. Phase 1 dropdown candidates are the five IDs above.

UI language keys (settings `language`): `ja` | `en` | `zh-Hans` | `zh-Hant` | `ko`.

## High value — Southeast Asia (future)

| ID | Language | Notes |
| --- | --- | --- |
| `vi` | Vietnamese | Telex/VNI — OS-level IME; highest SEA priority |
| `th` | Thai | Layout switch Latin ↔ Thai |
| `km` | Khmer | Layout switch |
| `my` | Myanmar (Burmese) | Layout switch |
| `lo` | Lao | Layout switch |

## Skip (low IME-Aura fit)

| ID | Language | Reason |
| --- | --- | --- |
| `id` | Indonesian | Latin-only; no IME mode toggle |
| `ms` | Malay | Latin-only |
| `fil` / `tl` | Filipino / Tagalog | Latin-only |

## Optional (later)

| ID | Language | Notes |
| --- | --- | --- |
| `hi` | Hindi | Phonetic IME |
| Indic family | bn / ta / te / … | Treat as family if added |
| `ar` / `he` | Arabic / Hebrew | Layout + RTL |
| `am` | Amharic | Ethiopic IME |
| Cantonese / Yi | via zh-Hant or niche IME | Lowest priority |

## Detection notes

- **Windows:** LANGID / keyboard layout + IMM conversion mode for Japanese native.
- **macOS:** TIS input source languages / source ID.
- **Linux:** ibus / fcitx engine name heuristics.

Firefly continues to use platform `*_is_japanese_input()` wrappers (`lang == ja`); Firefly cores are not part of this catalog work.
