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
enter_prepare (guint     from,
               guint    *to,
               gpointer  user_data,
               GError  **error)
{
  PasswordDaemon *daemon = user_data;
  g_autoptr(GSocket) control = NULL;

  g_assert_cmpuint (from, ==, PASSWORD_DAEMON_INITIAL);
  g_assert_cmpuint (*to, ==, PASSWORD_DAEMON_PREPARE);

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

The state machine commits the target state only after the callback succeeds. If
the callback fails, the state remains unchanged.

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

## Choosing the Landing State

The @to argument to [callback@Dex.StateTransitionFunc] is inout. On entry it
contains the target state for the current edge. If the callback leaves it
unchanged, the transition ends at that state.

If the callback changes @to, the state machine commits the changed state
instead. This does not execute another transition edge. The changed state must
be a valid value in the state enum.

This is useful for transient states such as `PREPARE` that should usually land
in another state such as `READY`.

```c
static gboolean
enter_prepare (guint     from,
               guint    *to,
               gpointer  user_data,
               GError  **error)
{
  PasswordDaemon *daemon = user_data;

  if (!dex_await (password_daemon_prepare (daemon), error))
    return FALSE;

  *to = PASSWORD_DAEMON_READY;

  return TRUE;
}
```

With this table, requesting `INITIAL -> PREPARE` runs `enter_prepare()` and
commits `READY` when the callback succeeds.

```c
static const DexStateTransition password_daemon_transitions[] = {
  { PASSWORD_DAEMON_INITIAL, PASSWORD_DAEMON_PREPARE, enter_prepare },
};
```

Changing @to may move forward or backward. It is a way for a transition to
choose its final landing state, not a way to request another state-machine
transition.

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

Callback failures leave the state unchanged. If a callback succeeds but changes
@to to an invalid enum value, the transition rejects and the state remains
unchanged.

## Related Pages

* [Limiters](limiters.html)
* [Scheduler behavior and pinning](schedulers.html)
