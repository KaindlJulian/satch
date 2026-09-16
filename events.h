#ifndef _events_h_INCLUDED
#define _events_h_INCLUDED

#ifdef EVENTS

#ifndef NBLOCK
#error "event logging needs './configure --events --no-block'"
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EVENTS_PROTOCOL_VERSION "1"
#define EVENTS_FLUSH_BYTES (1u << 18)

enum events_kind
{
  EVENTS_KIND_CONFLICT,
  EVENTS_KIND_RESTART,
  EVENTS_KIND_OTHER
};

enum events_outcome
{
  EVENTS_SATISFIED,
  EVENTS_UNIT,
  EVENTS_FALSIFIED,
  EVENTS_UNRESOLVED
};

// The solver flushes root-level literals off its own trail, while the
// protocol's fold keeps them.  So the writer folds its own copy, and the
// 'conflict' snapshots and the model read from that.

struct events_assignment
{
  int literal;
  unsigned level;
};

struct events
{
  FILE *file;
  int level;
  bool started;

  char *buffer;
  size_t size, capacity;

  struct events_assignment *trail;
  size_t trail_size, trail_capacity;

  enum events_kind kind;
  const char *reason;
};

static void
events_out_of_memory (void)
{
  fputs ("satch: out of memory writing event log\n", stderr);
  exit (1);
}

static const char *
events_kind_string (enum events_kind kind)
{
  switch (kind)
    {
    case EVENTS_KIND_CONFLICT:
      return "conflict";
    case EVENTS_KIND_RESTART:
      return "restart";
    default:
      return "other";
    }
}

static const char *
events_outcome_string (enum events_outcome outcome)
{
  switch (outcome)
    {
    case EVENTS_SATISFIED:
      return "satisfied";
    case EVENTS_UNIT:
      return "unit";
    case EVENTS_FALSIFIED:
      return "falsified";
    default:
      return "unresolved";
    }
}

static void
events_reserve (struct events *events, size_t bytes)
{
  if (events->size + bytes <= events->capacity)
    return;
  size_t capacity = events->capacity ? events->capacity : 4096;
  while (events->size + bytes > capacity)
    capacity *= 2;
  char *buffer = realloc (events->buffer, capacity);
  if (!buffer)
    events_out_of_memory ();
  events->buffer = buffer;
  events->capacity = capacity;
}

static void
events_char (struct events *events, char ch)
{
  events_reserve (events, 1);
  events->buffer[events->size++] = ch;
}

static void
events_string (struct events *events, const char *str)
{
  const size_t len = strlen (str);
  events_reserve (events, len);
  memcpy (events->buffer + events->size, str, len);
  events->size += len;
}

static void
events_number (struct events *events, int64_t value)
{
  char digits[24];
  size_t n = 0;
  uint64_t rest;
  if (value < 0)
    {
      events_char (events, '-');
      rest = -(uint64_t) value;
    }
  else
    rest = (uint64_t) value;
  do
    {
      digits[n++] = '0' + (char) (rest % 10);
      rest /= 10;
    }
  while (rest);
  events_reserve (events, n);
  while (n)
    events->buffer[events->size++] = digits[--n];
}

static void
events_open (struct events *events, const char *name)
{
  events_string (events, "{\"event\":\"");
  events_string (events, name);
  events_char (events, '"');
}

static void
events_key (struct events *events, const char *key)
{
  events_string (events, ",\"");
  events_string (events, key);
  events_string (events, "\":");
}

static void
events_text (struct events *events, const char *value)
{
  events_char (events, '"');
  events_string (events, value);
  events_char (events, '"');
}

static void
events_close (struct events *events)
{
  events_string (events, "}\n");
  if (events->size < EVENTS_FLUSH_BYTES)
    return;
  fwrite (events->buffer, 1, events->size, events->file);
  events->size = 0;
}

static void
events_flush (struct events *events)
{
  if (events->size)
    fwrite (events->buffer, 1, events->size, events->file);
  events->size = 0;
  fflush (events->file);
}

static void
events_trail_push (struct events *events, int literal, unsigned level)
{
  if (events->trail_size == events->trail_capacity)
    {
      const size_t capacity =
	events->trail_capacity ? 2 * events->trail_capacity : 1024;
      struct events_assignment *trail =
	realloc (events->trail, capacity * sizeof *trail);
      if (!trail)
	events_out_of_memory ();
      events->trail = trail;
      events->trail_capacity = capacity;
    }
  struct events_assignment *assignment = events->trail + events->trail_size++;
  assignment->literal = literal;
  assignment->level = level;
}

static void
events_trail_keep (struct events *events, unsigned level)
{
  size_t kept = 0;
  for (size_t i = 0; i != events->trail_size; i++)
    if (events->trail[i].level <= level)
      events->trail[kept++] = events->trail[i];
  events->trail_size = kept;
}

static void
events_trail (struct events *events)
{
  events_char (events, '[');
  for (size_t i = 0; i != events->trail_size; i++)
    {
      if (i)
	events_char (events, ',');
      events_number (events, events->trail[i].literal);
    }
  events_char (events, ']');
}

static void
events_release (struct events *events)
{
  free (events->buffer);
  free (events->trail);
  events->buffer = 0;
  events->trail = 0;
  events->size = events->capacity = 0;
  events->trail_size = events->trail_capacity = 0;
}

#endif

#endif
