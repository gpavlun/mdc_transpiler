# MDC transpiler — version 0.3

A small standalone C program that extracts language-labeled fenced code blocks
from `.mdc` and `.mdh` documents into neighboring `.c` and `.h` files. Markdown
documents are the source of truth; generated files are disposable artifacts.

## Build

On Linux/macOS with a C compiler and POSIX `getline()`, `mkstemp()`, and
`rename()`:

```sh
make
```

Or compile directly:

```sh
cc -std=c11 -O2 -Wall -Wextra -Wpedantic src/mdc.c -o mdc
```

## Use

```sh
./mdc path/to/file.mdc
./mdc path/to/file.mdh
```

`file.mdc` generates `file.c`. If a sibling `file.mdh`
exists, it is always transpiled first to regenerate `file.h`, even when
that header already exists. If header extraction fails, the `.c` file is not
generated during that invocation. Running `mdc` directly on an `.mdh` always
regenerates its `.h` output.

If no matching `.mdh` exists, a pre-existing `.h` is left untouched. This
allows an `.mdc` file to use an ordinary hand-maintained header when no Markdown
header source is provided.

## Extraction rules

- `.mdc` maps to `.c`; `.mdh` maps to `.h`. Both currently target C.
- The first token in a fenced block's info string selects its language.
- The C fence identifier is `c`, matched case-insensitively. `c`, `C`, `c title="API"`,
  and `C example` are all extracted. The remainder of the info string is ignored.
- Unlabeled fences and fences labeled for other languages are ignored.
- Backtick and tilde fences are recognized, with at least three characters.
- Opening and closing fences may have up to three leading spaces.
- A closing fence must use the same marker, be at least as long as the opener,
  and contain only spaces or tabs after the marker run.
- Matching code blocks are copied in document order. Markdown and fence lines
  are omitted. A document with no matching blocks generates an empty file.
- An unclosed fence is an error. The previous generated output is preserved.
- Output is written to a temporary file in the destination directory and
  renamed into place only after successful extraction.
- The transpiler does not parse or compile the extracted language.

## Language extensibility

Language selection is split into two concepts: the document extension determines
the target language and output extension, while fence aliases determine which
blocks are extracted. The current implementation supports only C; additional
languages and aliases can be added in a future version.

## Limitations

- This is a small fence scanner, not a complete CommonMark parser.
- The implementation is POSIX-oriented because it uses `getline()` and `mkstemp()`.


## VS code extension
[vscode extension repo](https://github.com/gpavlun/mdc_vscode_extension/blob/master/README.md)
You should probably add the binary to your path if you want to use the extension
