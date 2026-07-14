Title: State Machines

[class@Dex.StateMachine] provides a small table-driven state machine for
asynchronous workflows.

Use it when an object has a finite set of modes and transitions between those
modes may need to wait for asynchronous work. Common examples include
connection lifecycles, authentication handoff, device setup, or UI workflows
where a request must move through well-defined states.

The state machine serializes transition requests internally, runs transition
callbacks from a fiber, and rejects attempts to use edges that were not declared
up front.

## Defining States

States are represented by a `GEnum` type. This lets libdex validate transition
tables and return enum values from [method@Dex.StateMachine.transition].

```c
typedef enum
{
  PASSWORD_DAEMON_INITIAL,
  PASSWORD_DAEMON_PREPARE,
  PASSWORD_DAEMON_READY,
  PASSWORD_DAEMON_LOCKED,
  PASSWORD_DAEMON_UNLOCKED,
} PasswordDaemonState;
```

The enum must have a registered `GType`, for example using
`G_DEFINE_ENUM_TYPE()`.

## Defining Transitions

A transition table declares every supported edge.

```c
static const DexStateTransition password_daemon_transitions[] = {
  { PASSWORD_DAEMON_INITIAL,  PASSWORD_DAEMON_PREPARE,  enter_prepare  },
  { PASSWORD_DAEMON_PREPARE,  PASSWORD_DAEMON_READY,    enter_ready    },
  { PASSWORD_DAEMON_READY,    PASSWORD_DAEMON_LOCKED,   enter_locked   },
  { PASSWORD_DAEMON_LOCKED,   PASSWORD_DAEMON_UNLOCKED, enter_unlocked },
  { PASSWORD_DAEMON_UNLOCKED, PASSWORD_DAEMON_LOCKED,   enter_locked   },
};
```

The table is copied and validated when the [class@Dex.StateMachine] is created.
Each entry must use valid enum values and have a transition callback. Duplicate
edges are rejected.

Once created, the transition table is immutable. There is no API to add
transitions while the state machine is running.

## Creating a State Machine

The constructor accepts user data that is passed to every transition callback.
Use normal C ownership rules for this data. If the state machine should keep an
object alive, pass a strong reference and destroy notify. If it should not,
pass a weak-reference holder instead.

```c
daemon->state_machine =
  dex_state_machine_new (PASSWORD_DAEMON_TYPE_STATE,
                         PASSWORD_DAEMON_INITIAL,
                         password_daemon_transitions,
                         G_N_ELEMENTS (password_daemon_transitions),
                         NULL,
                         0,
                         g_object_ref (daemon),
                         g_object_unref);
```

## Writing Transition Callbacks

Transition callbacks use [callback@Dex.StateTransitionFunc].

```c
static gboolean
enter_prepare (DexStateTransitionContext *context,
               gpointer                   user_data,
               GError                   **error)
{
  PasswordDaemon *daemon = user_data;
  g_autoptr(GSocket) control = NULL;

  g_assert_cmpuint (dex_state_transition_context_get_from (context),
                    ==,
                    PASSWORD_DAEMON_INITIAL);
  g_assert_cmpuint (dex_state_transition_context_get_to (context),
                    ==,
                    PASSWORD_DAEMON_PREPARE);

  control = dex_await_object (create_control_socket (daemon), error);
  if (control == NULL)
    return FALSE;

  daemon->control = g_steal_pointer (&control);

  return TRUE;
}
```

Callbacks are run from a fiber, so they may call `dex_await()` and
`dex_await_*()` directly. They should return %TRUE on success or %FALSE with
@error set on failure.

If a callback succeeds without calling
[method@Dex.StateTransitionContext.set_state], the state machine commits the
target state from [method@Dex.StateTransitionContext.get_to]. If the callback
fails before setting state, the state remains unchanged.

Callbacks may also call [method@Dex.StateTransitionContext.continue_to] to
continue through another declared edge before queued transition requests are
processed.

## Requesting Transitions

Call [method@Dex.StateMachine.transition] to request a transition. The returned
future resolves to the final enum state or rejects with an error.

```c
DexFuture *future =
  dex_state_machine_transition (daemon->state_machine,
                                PASSWORD_DAEMON_LOCKED);
```

Requests are serialized. If multiple callers request transitions at once, only
one transition fiber runs at a time.

Unsupported transitions reject with
[error@Dex.Error.INVALID_TRANSITION]. For example, a request to go directly
from `PASSWORD_DAEMON_INITIAL` to `PASSWORD_DAEMON_UNLOCKED` will fail unless
that edge exists in the transition table.

## Updating State During a Transition

[struct@Dex.StateTransitionContext] gives callbacks access to both the declared
transition edge and the real current state. The context is only valid for the
duration of the transition callback and must not be stored.

[method@Dex.StateTransitionContext.get_from] and
[method@Dex.StateTransitionContext.get_to] are fixed for the callback. They
describe the transition table edge being executed. They do not change if the
callback updates the state.

[method@Dex.StateTransitionContext.get_state] reads the real current state
from the state machine. [method@Dex.StateTransitionContext.set_state] updates
that real state immediately. This is useful for quiescent transitions that need
to expose progress through intermediate states in one pass. `set_state()` may
only be used while the transition callback is active.

```c
static gboolean
enter_prepare (DexStateTransitionContext *context,
               gpointer                   user_data,
               GError                   **error)
{
  PasswordDaemon *daemon = user_data;

  dex_state_transition_context_set_state (context, PASSWORD_DAEMON_PREPARE);

  if (!dex_await (password_daemon_prepare (daemon), error))
    return FALSE;

  dex_state_transition_context_set_state (context, PASSWORD_DAEMON_READY);

  return TRUE;
}
```

With this table, requesting `INITIAL -> PREPARE` runs `enter_prepare()` and
resolves with `READY` when the callback succeeds.

```c
static const DexStateTransition password_daemon_transitions[] = {
  { PASSWORD_DAEMON_INITIAL, PASSWORD_DAEMON_PREPARE, enter_prepare },
};
```

If a callback calls [method@Dex.StateTransitionContext.set_state], those
updates are not rolled back automatically if the callback later fails. They are
real state changes. Set an explicit failure state before returning %FALSE when
that is the state machine behavior you want.

## GObject State Properties

[class@Dex.StateMachine] is a `DexObject`, not a `GObject`, so it does not have
`GObject` properties and does not emit `notify::state`. When a `GObject` owns a
state machine, keep the state machine private and expose state from the owning
object.

Install a readable enum property on the owner. Transition callbacks will emit
`notify::state` explicitly when observable state changes.

```c
enum {
  PROP_0,
  PROP_STATE,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

static PasswordDaemonState
password_daemon_get_state (PasswordDaemon *daemon)
{
  return dex_state_machine_get_state (daemon->state_machine);
}

static void
password_daemon_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  PasswordDaemon *daemon = PASSWORD_DAEMON (object);

  switch (prop_id)
    {
    case PROP_STATE:
      g_value_set_enum (value, password_daemon_get_state (daemon));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
password_daemon_class_init (PasswordDaemonClass *daemon_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (daemon_class);

  object_class->get_property = password_daemon_get_property;

  properties[PROP_STATE] =
    g_param_spec_enum ("state", NULL, NULL,
                       PASSWORD_DAEMON_TYPE_STATE,
                       PASSWORD_DAEMON_INITIAL,
                       (G_PARAM_READABLE |
                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}
```

If the owner stores a strong reference to the state machine, do not also pass a
strong reference to the owner as transition user data. That creates a reference
cycle:

```c
daemon -> state_machine -> daemon
```

Use a small weak-reference holder instead.

```c
typedef struct
{
  GWeakRef daemon;
} PasswordDaemonTransitionData;

static PasswordDaemonTransitionData *
password_daemon_transition_data_new (PasswordDaemon *daemon)
{
  PasswordDaemonTransitionData *data;

  data = g_new0 (PasswordDaemonTransitionData, 1);
  g_weak_ref_init (&data->daemon, daemon);

  return data;
}

static void
password_daemon_transition_data_free (PasswordDaemonTransitionData *data)
{
  g_weak_ref_clear (&data->daemon);
  g_free (data);
}

daemon->state_machine =
  dex_state_machine_new (PASSWORD_DAEMON_TYPE_STATE,
                         PASSWORD_DAEMON_INITIAL,
                         password_daemon_transitions,
                         G_N_ELEMENTS (password_daemon_transitions),
                         NULL,
                         0,
                         password_daemon_transition_data_new (daemon),
                         (GDestroyNotify)password_daemon_transition_data_free);
```

When transition callbacks update the state machine state directly, notify the
owner at the same time. A helper keeps this consistent.

```c
static void
password_daemon_set_state (PasswordDaemon            *daemon,
                           DexStateTransitionContext *context,
                           PasswordDaemonState        state)
{
  if (dex_state_transition_context_get_state (context) == state)
    return;

  dex_state_transition_context_set_state (context, state);
  g_object_notify_by_pspec (G_OBJECT (daemon), properties[PROP_STATE]);
}
```

Then transition callbacks can acquire the owner from the weak reference and use
the helper for every state change that should be visible through `:state`.

```c
static gboolean
enter_prepare (DexStateTransitionContext  *context,
               gpointer                    user_data,
               GError                    **error)
{
  PasswordDaemonTransitionData *data = user_data;
  g_autoptr(PasswordDaemon) daemon = NULL;

  daemon = g_weak_ref_get (&data->daemon);
  if (daemon == NULL)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_CANCELLED,
                           "PasswordDaemon was disposed");
      return FALSE;
    }

  password_daemon_set_state (daemon, context, PASSWORD_DAEMON_PREPARE);

  if (!dex_await (password_daemon_prepare (daemon), error))
    return FALSE;

  password_daemon_set_state (daemon, context, PASSWORD_DAEMON_READY);

  return TRUE;
}
```

If `:state` notifications matter, do not rely on the state machine's automatic
target commit for those states. The automatic commit happens after the
transition callback returns, where the owner has no callback-local opportunity
to emit `notify::state`. Call [method@Dex.StateTransitionContext.set_state]
through your notification helper instead.

The same rule applies when using [method@Dex.StateTransitionContext.continue_to].
If the current edge target should be observable, set and notify it before
calling `continue_to()`. The continued edge callback should notify its own
observable state changes.

Notifications are emitted from the transition fiber. If signal handlers for
the owner must run on a specific main context or thread, create and use the
state machine with a scheduler that runs there.

## Continuing Through Edges

[method@Dex.StateTransitionContext.continue_to] lets a transition callback run
another declared edge immediately while the state machine still owns its
serialization slot. It is synchronous composition: the next transition
callback is called before `continue_to()` returns, and queued transition
requests wait until the continuation chain finishes.

```c
static gboolean
enter_prepare (DexStateTransitionContext  *context,
               gpointer                    user_data,
               GError                    **error)
{
  PasswordDaemon *daemon = user_data;

  if (!dex_await (password_daemon_prepare (daemon), error))
    return FALSE;

  return dex_state_transition_context_continue_to (context,
                                                   PASSWORD_DAEMON_READY,
                                                   error);
}
```

With this table, a request for `INITIAL -> PREPARE` runs `enter_prepare()`,
commits `PREPARE`, then immediately runs `enter_ready()` before any queued
transition request can run.

```c
static const DexStateTransition password_daemon_transitions[] = {
  { PASSWORD_DAEMON_INITIAL, PASSWORD_DAEMON_PREPARE, enter_prepare },
  { PASSWORD_DAEMON_PREPARE, PASSWORD_DAEMON_READY,   enter_ready   },
};
```

If the callback has not called [method@Dex.StateTransitionContext.set_state],
`continue_to()` validates the next edge before committing the current edge. An
unsupported continuation returns %FALSE with
[error@Dex.Error.INVALID_TRANSITION]. If the next callback fails after the
current edge has been committed, that state change is not rolled back.

If a continued transition callback uses `dex_await()`, the same transition
fiber suspends and later resumes; the main context is not blocked, but the
state machine queue is still held. Chained continuations use ordinary C calls,
so this API is intended for short, bounded handoffs between edges rather than
unbounded graph traversal.

## Circular State Machines

Circular graphs are valid. For example, a lock state machine can declare both
directions explicitly.

```c
static const DexStateTransition lock_transitions[] = {
  { PASSWORD_DAEMON_LOCKED,   PASSWORD_DAEMON_UNLOCKED, unlock },
  { PASSWORD_DAEMON_UNLOCKED, PASSWORD_DAEMON_LOCKED,   lock   },
};
```

The state machine does not reject cycles. It rejects only invalid states,
duplicate edges at construction time, and unsupported edges at transition time.

## Error Handling

There are three common failure cases:

* Construction fails if the transition table is invalid.
* Transition requests reject with [error@Dex.Error.INVALID_TRANSITION] when no
  declared edge exists.
* Transition callbacks may fail with their own error.

Callback failures leave the state unchanged only if the callback has not
already called [method@Dex.StateTransitionContext.set_state]. Invalid explicit
state updates are programmer errors.

## Related Pages

* [Limiters](limiters.html)
* [Scheduler behavior and pinning](schedulers.html)
