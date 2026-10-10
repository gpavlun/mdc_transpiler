#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct fence {
  char marker;
  size_t length;
  size_t opening_line;
  int extract;
};

static const char *const c_fence_aliases[] = {
  "c",
  NULL
};

static void usage(const char *program) {
  fprintf(stderr, "Usage: %s <input.mdc|input.mdh>\n", program);
}

static int ends_with(const char *text, const char *suffix) {
  size_t text_length = strlen(text);
  size_t suffix_length = strlen(suffix);
  return text_length >= suffix_length &&
    strcmp(text + text_length - suffix_length, suffix) == 0;
}

static char *replace_extension(const char *path, const char *old_extension,
    const char *new_extension) {
  size_t path_length = strlen(path);
  size_t old_length = strlen(old_extension);
  size_t new_length = strlen(new_extension);
  size_t stem_length;
  char *result;

  if (!ends_with(path, old_extension)) {
    return NULL;
  }

  stem_length = path_length - old_length;
  result = malloc(stem_length + new_length + 1);
  if (result == NULL) {
    fprintf(stderr, "mdc: out of memory\n");
    return NULL;
  }

  memcpy(result, path, stem_length);
  memcpy(result + stem_length, new_extension, new_length + 1);
  return result;
}

static char *make_output_path(const char *input_path) {
  if (ends_with(input_path, ".mdc")) {
    return replace_extension(input_path, ".mdc", ".c");
  }
  if (ends_with(input_path, ".mdh")) {
    return replace_extension(input_path, ".mdh", ".h");
  }

  fprintf(stderr, "mdc: unsupported input extension: %s\n", input_path);
  return NULL;
}

static size_t content_length(const char *line, size_t length) {
  while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
    --length;
  }
  return length;
}

static int is_space_or_tab(char c) {
  return c == ' ' || c == '\t';
}

static int matches_alias(const char *token, size_t token_length,
    const char *const *aliases) {
  size_t i;

  for (i = 0; aliases[i] != NULL; ++i) {
    size_t alias_length = strlen(aliases[i]);
    size_t j;
    int matches = token_length == alias_length;

    for (j = 0; matches && j < token_length; ++j) {
      if (tolower((unsigned char)token[j]) !=
          tolower((unsigned char)aliases[i][j])) {
        matches = 0;
      }
    }
    if (matches) {
      return 1;
    }
  }
  return 0;
}

/* Recognize a fenced-block opener with up to three leading spaces. */
static int parse_opening_fence(const char *line, size_t length, struct fence *out) {
  size_t end = content_length(line, length);
  size_t i = 0;
  size_t run = 0;
  size_t info_start;
  size_t info_end;

  while (i < end && i < 4 && line[i] == ' ') {
    ++i;
  }
  if (i > 3 || i >= end || (line[i] != '`' && line[i] != '~')) {
    return 0;
  }

  out->marker = line[i];
  while (i + run < end && line[i + run] == out->marker) {
    ++run;
  }
  if (run < 3) {
    return 0;
  }

  info_start = i + run;
  while (info_start < end && is_space_or_tab(line[info_start])) {
    ++info_start;
  }
  info_end = info_start;
  while (info_end < end && !is_space_or_tab(line[info_end])) {
    ++info_end;
  }

  /* Markdown forbids backticks in the info string of a backtick fence. */
  if (out->marker == '`') {
    size_t j;
    for (j = info_start; j < end; ++j) {
      if (line[j] == '`') {
        return 0;
      }
    }
  }

  out->length = run;
  out->extract = matches_alias(line + info_start, info_end - info_start,
    c_fence_aliases);
  return 1;
}

static int is_closing_fence(const char *line, size_t length, const struct fence *fence) {
  size_t end = content_length(line, length);
  size_t i = 0;
  size_t run = 0;

  while (i < end && i < 4 && line[i] == ' ') {
    ++i;
  }
  if (i > 3 || i >= end || line[i] != fence->marker) {
    return 0;
  }

  while (i + run < end && line[i + run] == fence->marker) {
    ++run;
  }
  if (run < fence->length) {
    return 0;
  }

  i += run;
  while (i < end && is_space_or_tab(line[i])) {
    ++i;
  }
  return i == end;
}

/* Extract to a temporary file, then atomically replace the generated output. */
static int transpile(const char *input_path, const char *output_path) {
  FILE *input = NULL;
  FILE *output = NULL;
  char *line = NULL;
  char *temporary_path = NULL;
  size_t capacity = 0;
  ssize_t length;
  size_t line_number = 0;
  int in_fence = 0;
  int failed = 0;
  int temporary_fd = -1;
  struct fence active_fence = {0};
  size_t temporary_path_length;

  input = fopen(input_path, "rb");
  if (input == NULL) {
    fprintf(stderr, "mdc: cannot open '%s': %s\n", input_path, strerror(errno));
    return 1;
  }

  temporary_path_length = strlen(output_path) + sizeof(".tmp.XXXXXX");
  temporary_path = malloc(temporary_path_length);
  if (temporary_path == NULL) {
    fprintf(stderr, "mdc: out of memory\n");
    fclose(input);
    return 1;
  }
  snprintf(temporary_path, temporary_path_length, "%s.tmp.XXXXXX", output_path);

  temporary_fd = mkstemp(temporary_path);
  if (temporary_fd < 0) {
    fprintf(stderr, "mdc: cannot create temporary output for '%s': %s\n",
      output_path, strerror(errno));
    free(temporary_path);
    fclose(input);
    return 1;
  }
  output = fdopen(temporary_fd, "wb");
  if (output == NULL) {
    fprintf(stderr, "mdc: cannot open temporary output for '%s': %s\n",
      output_path, strerror(errno));
    close(temporary_fd);
    remove(temporary_path);
    free(temporary_path);
    fclose(input);
    return 1;
  }

  while ((length = getline(&line, &capacity, input)) != -1) {
    ++line_number;

    if (!in_fence) {
      struct fence opening = {0};
      if (parse_opening_fence(line, (size_t)length, &opening)) {
        opening.opening_line = line_number;
        active_fence = opening;
        in_fence = 1;
      }
      continue;
    }

    if (is_closing_fence(line, (size_t)length, &active_fence)) {
      in_fence = 0;
      continue;
    }

    if (active_fence.extract && fwrite(line, 1, (size_t)length, output) != (size_t)length) {
      fprintf(stderr, "mdc: failed writing temporary output for '%s'\n", output_path);
      failed = 1;
      break;
    }
  }

  if (ferror(input)) {
    fprintf(stderr, "mdc: failed reading '%s'\n", input_path);
    failed = 1;
  }
  if (!failed && in_fence) {
    fprintf(stderr, "mdc: unclosed fence in '%s', opened on line %zu\n",
      input_path, active_fence.opening_line);
    failed = 1;
  }

  free(line);
  if (fclose(input) != 0) {
    fprintf(stderr, "mdc: failed closing '%s'\n", input_path);
    failed = 1;
  }
  if (fclose(output) != 0) {
    fprintf(stderr, "mdc: failed closing temporary output for '%s'\n", output_path);
    failed = 1;
  }

  if (!failed && rename(temporary_path, output_path) != 0) {
    fprintf(stderr, "mdc: cannot replace '%s': %s\n", output_path, strerror(errno));
    failed = 1;
  }
  if (failed) {
    if (remove(temporary_path) != 0 && errno != ENOENT) {
      fprintf(stderr, "mdc: warning: could not remove temporary file '%s'\n", temporary_path);
    }
  }

  free(temporary_path);
  return failed ? 1 : 0;
}

/* For an .mdc input, regenerate its sibling .h from the matching .mdh if present. */
static int regenerate_matching_header(const char *source_path) {
  char *header_source = replace_extension(source_path, ".mdc", ".mdh");
  char *header_output = NULL;
  int result = 0;

  if (header_source == NULL) {
    fprintf(stderr, "mdc: out of memory while deriving matching header path\n");
    return 1;
  }

  if (access(header_source, F_OK) != 0) {
    if (errno != ENOENT) {
      fprintf(stderr, "mdc: cannot access '%s': %s\n", header_source, strerror(errno));
      result = 1;
    }
    free(header_source);
    return result;
  }

  header_output = make_output_path(header_source);
  if (header_output == NULL) {
    free(header_source);
    return 1;
  }

  result = transpile(header_source, header_output);
  if (result == 0) {
    printf("%s -> %s\n", header_source, header_output);
  }

  free(header_output);
  free(header_source);
  return result;
}

int main(int argc, char **argv) {
  char *output_path;
  int result;

  if (argc != 2) {
    usage(argv[0]);
    return 2;
  }

  if (!ends_with(argv[1], ".mdc") && !ends_with(argv[1], ".mdh")) {
    fprintf(stderr, "mdc: unsupported input extension: %s\n", argv[1]);
    usage(argv[0]);
    return 2;
  }

  /* Regenerate the matching header first, so a failed header parse stops the build step. */
  if (ends_with(argv[1], ".mdc")) {
    result = regenerate_matching_header(argv[1]);
    if (result != 0) {
      return result;
    }
  }

  output_path = make_output_path(argv[1]);
  if (output_path == NULL) {
    return 2;
  }

  result = transpile(argv[1], output_path);
  if (result == 0) {
    printf("%s -> %s\n", argv[1], output_path);
  }
  free(output_path);
  return result;
}
