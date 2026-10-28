Title: Dependency Graphs

[struct@Dex.GraphBuilder] collects operations and the dependencies between
them. Once the graph is built, it is a [class@Dex.Future] which starts ready
operations when observed.

This is useful when the dependencies come from data. For example, a build
description may contain commands contributed by different plugins. A code
generator must finish before compiling sources that include its output, and
linking must wait for all of the object files. You want to collect those
relationships, check for cycles, and then execute the plan.

Future sets can wait for several operations, and `dex_future_then()` can start
work after another operation resolves. Expressing a graph assembled at runtime
with those APIs also requires application code to track dependencies, validate
cycles, and share prerequisites. You can instead use the graph builder to
handle that work for you.

The public API uses `DexGraphBuilder` and `DexGraphNode`. Building returns a
`DexFuture`; there is no separate public `DexGraph` type to manage.

Dependency graphs are available since libdex 1.3.

## Describing Operations

Use [class@Dex.Deferred] for operations whose startup the graph should control.
Adding a future to a builder does not observe it. Building the graph validates
the dependencies without starting the deferred operations.

For a build plan, we can describe each step with an argument vector and the
indices of the steps it depends on.

```c
typedef struct
{
  const char * const *argv;
  const guint        *dependencies;
  guint               n_dependencies;
} BuildStep;

static DexFuture *
run_command (gpointer user_data)
{
  const char * const *argv = user_data;
  g_autoptr(GSubprocess) subprocess = NULL;
  g_autoptr(GError) error = NULL;

  g_assert (argv != NULL);
  g_assert (argv[0] != NULL);

  if (!(subprocess = g_subprocess_newv (argv, G_SUBPROCESS_FLAGS_NONE, &error)))
    return dex_future_new_for_error (g_steal_pointer (&error));

  return dex_subprocess_wait_check (subprocess);
}
```

`run_command()` starts a subprocess and returns a future for its successful
exit. It can run as a deferred callback because waiting for the process is
asynchronous.

The following helper adds every operation before connecting the dependencies.
This allows a step to refer to another step which appears later in the input.
The caller provides nonempty argument vectors and valid dependency indices;
these would normally be checked while parsing the build description.

```c
static DexFuture *
create_build_graph (const BuildStep  *steps,
                    guint             n_steps,
                    GError          **error)
{
  g_autoptr(DexGraphBuilder) builder = NULL;
  g_autofree DexGraphNode **nodes = NULL;

  g_assert (steps != NULL || n_steps == 0);
  g_assert (error == NULL || *error == NULL);

  builder = dex_graph_builder_new (NULL);
  nodes = g_new0 (DexGraphNode *, n_steps);

  for (guint i = 0; i < n_steps; i++)
    {
      DexDeferred *deferred;

      deferred = dex_deferred_new (NULL,
                                   run_command,
                                   g_strdupv ((char **)steps[i].argv),
                                   (GDestroyNotify)g_strfreev);
      dex_future_set_static_name (DEX_FUTURE (deferred), g_intern_string (steps[i].argv[0]));
      nodes[i] = dex_graph_builder_add (builder, DEX_FUTURE (deferred));
    }

  for (guint i = 0; i < n_steps; i++)
    {
      for (guint j = 0; j < steps[i].n_dependencies; j++)
        {
          guint dependency = steps[i].dependencies[j];

          g_assert (dependency < n_steps);

          dex_graph_builder_add_dependency (builder, nodes[i], nodes[dependency]);
        }
    }

  return dex_graph_builder_build (builder, error);
}
```

[method@Dex.GraphBuilder.add] takes ownership of the future. Each deferred
owns a copy of its argument vector, so the input description can be released
after this function returns. The builder can also be freed after a successful
build because the graph keeps the operations alive.

[method@Dex.GraphBuilder.add_dependency] takes the waiting node first and its
prerequisite second. Both handles must belong to the same builder. Repeating
a dependency is harmless. Node handles are only used while configuring the
builder, and a successful build consumes the builder's nodes. Use a new
builder for another plan.

## Running a Build Plan

For example, the following description generates a header, compiles two source
files which include it, and links the resulting object files. The argument
vectors are illustrative commands for a project with a `generate-header`
tool, `main.c`, and `helpers.c`.

```c
const char *generate_argv[] = { "./generate-header", "generated.h", NULL };
const char *main_argv[] = { "cc", "-c", "main.c", "-o", "main.o", NULL };
const char *helpers_argv[] = { "cc", "-c", "helpers.c", "-o", "helpers.o", NULL };
const char *link_argv[] = { "cc", "main.o", "helpers.o", "-o", "example", NULL };
const guint compile_dependencies[] = { 0 };
const guint link_dependencies[] = { 1, 2 };
const BuildStep steps[] = {
  { generate_argv, NULL, 0 },
  { main_argv, compile_dependencies, G_N_ELEMENTS (compile_dependencies) },
  { helpers_argv, compile_dependencies, G_N_ELEMENTS (compile_dependencies) },
  { link_argv, link_dependencies, G_N_ELEMENTS (link_dependencies) },
};
g_autoptr(GError) error = NULL;
DexFuture *graph;

if (!(graph = create_build_graph (steps, G_N_ELEMENTS (steps), &error)))
  return dex_future_new_for_error (g_steal_pointer (&error));

if (!dex_await (graph, &error))
  return dex_future_new_for_error (g_steal_pointer (&error));
```

This code runs from a fiber. As with other libdex programs, call `dex_init()`
from `main()` before using the library.

The generator runs once and both compile steps wait for it. Once it resolves,
the compilers can run concurrently. Linking starts only after both compilers
have resolved. The graph resolves to `TRUE` after all operations succeed.
An empty graph also resolves to `TRUE`.

The same helper can consume steps read from a build description without
changing the execution code. The graph is fixed after building; create a new
graph when the plan changes or when the build needs to run again.

## Handling Failure

[method@Dex.GraphBuilder.build] returns `NULL` and sets
[error@Dex.Error.INVALID_TRANSITION] if the dependencies contain a cycle.
This happens before the graph observes any operation. Naming the futures
before adding them helps identify nodes in cycle diagnostics.

During execution, a rejected prerequisite prevents its dependent operations
from starting. If header generation fails in this example, neither compiler
nor the linker starts. If one compiler fails, the other compiler may finish,
but linking does not start.

Independent branches continue. The graph waits for all branches to finish or
be skipped before rejecting with [error@Dex.Error.DEPENDENCY_FAILED]. It does
not return the original operation's error. If the application needs that
error or an operation's value, keep an extra reference to that operation's
future before passing it to [method@Dex.GraphBuilder.add], then inspect its
result after the graph completes. A skipped deferred may still be pending;
check its status instead of awaiting it and starting the skipped work.

Dependencies describe ordering. They do not pass result values as callback
arguments. The build example exchanges its results through generated files.
Other applications can keep prerequisite futures in callback data and read
their resolved values when the dependent operation starts.

## Scheduling and Ownership

The builder's scheduler coordinates readiness. Each deferred keeps its own
execution target, so one graph can combine asynchronous callbacks, fibers,
coroutines, and blocking work on a thread pool.

The graph observes a node when all its prerequisites have resolved. Adding an
already running operation cannot postpone that operation. Observing a
deferred node separately also starts it independently of the graph, so leave
observation to the graph when its dependencies must control startup.

All ready operations may run concurrently. If the build needs a limit on
concurrent commands, have each deferred start work through a
[class@Dex.Limiter]. Acquire capacity when the node becomes ready so waiting
for dependencies does not occupy a permit.

Discarding the graph's last observer propagates discard to the futures it
observes. Operations with other observers can continue, and work which has
started follows its own cancellation behavior. In this example, cancelling a
subprocess wait does not terminate the subprocess. Applications which need
that behavior must also manage the process lifetime.

[class@Dex.TaskGroup] provides a scope for dynamically added futures and
explicit cancellation of tracked children. A graph provides the fixed
dependency relationships that decide when operations may start. These serve
different parts of an application's work management.
