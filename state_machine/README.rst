#################################################
Introduction to State Machine Framework in Zephyr
#################################################

The State Machine Framework (SMF) is an application agnostic framework that provides an easy way for developers to integrate state machines into their application. The framework can be added to any project by enabling the `CONFIG_SMF` option.

==================
List of KConfig(s)
==================

        CONFIG_SMF
        CONFIG_SMF_ANCESTOR_SUPPORT
        CONFIG_SMF_INITIAL_TRANSITION

==============
State Creation
==============

A state is represented by three functions, where one function implements the Entry actions, another function implements the Run actions, and the last function implements the Exit actions.

 * Entry
 * Run
 * Exit

The prototype for the entry and exit functions are as follows:
.. code-block:: c
        void funct(void *obj),

and the prototype for the run action is
.. code-block:: c
        enum smf_state_result funct(void *obj)

where the `obj` parameter is a user defined structure that has the state machine context, `smf_ctx`, as its first member. For example:

.. code-block:: c
        struct user_object {
        struct smf_ctx ctx;
        /* All User Defined Data Follows */
        };

The `smf_ctx` member must be first because the state machine framework’s functions casts the user defined object to the smf_ctx type with the SMF_CTX macro.

For example instead of doing this
.. code-block:: c
        (struct smf_ctx *)&user_obj;

you could use
.. code-block:: c
        SMF_CTX(&user_obj).

========================================
Hierarchical or Anchestral State Machine
========================================

By default, a state can have no ancestor states, resulting in a flat state machine. But to enable the creation of a hierarchical state machine, the `CONFIG_SMF_ANCESTOR_SUPPORT` option must be enabled.

*******************************************
Significance of Hierarchical State Machines
*******************************************

In a standard, flat state machine, if you have a common behavior—like a "Low Battery" or "Power Off" event—every single state must have its own transition to handle it. If you have 10 states, you need 10 identical transition lines. This is called `state explosion`, and it makes diagrams and code incredibly messy and hard to maintain.

--------------------------------------
Solution : Hierarchical State Machines
--------------------------------------

 +------------------ [ Active State (Parent) ] ------------------+
 |                                                               |
 |   [ State A (Child) ] ---------> [ State B (Child) ]          |
 |                                                               |
 +---------------------------------------------------------------+
        ↓
   (Power Off)
        ↓
  [ Power Down ]

 **Inheritance**: If an event occurs, the system first looks at the currently active child state (e.g., State A).

 **Delegation**: If State A doesn't know how to handle the event, it passes it up to its parent state (Active State).

 **Common Transitions**: Because Active State has a transition for Power Off, the system handles the event there. You only had to define the "Power Off" behavior once instead of for every individual child state.

-----------------------------------
Usage of Hierarchical State Machine
-----------------------------------

The return value of the run action, `enum smf_state_result` determines if the state machine propagates the event to parent run actions (`SMF_EVENT_PROPAGATE`) or if the event was handled by the run action (`SMF_EVENT_HANDLED`).

* Flat state machines do not have parent actions, so the return code is ignored; returning `SMF_EVENT_HANDLED` is recommended (Future-Proof Code for Later Versions of Zephyr).
* Calling `smf_set_state()` prevents calling parent run actions, even if `SMF_EVENT_PROPAGATE` is returned.

By default, the hierarchical state machines do not support initial transitions to child states on entering a superstate. To enable them the `CONFIG_SMF_INITIAL_TRANSITION` option must be enabled.

The following macro can be used for easy state creation:
.. code-block:: C
        SMF_CREATE_STATE Create a state

======================
State Machine Creation
======================

A state machine is created by defining a table of states that’s indexed by an enum. For example, the following creates three flat states:

.. code-block:: C
        enum demo_state { S0, S1, S2 };

        const struct smf_state demo_states[] = {
        [S0] = SMF_CREATE_STATE(s0_entry, s0_run, s0_exit, NULL, NULL),
        [S1] = SMF_CREATE_STATE(s1_entry, s1_run, s1_exit, NULL, NULL),
        [S2] = SMF_CREATE_STATE(s2_entry, s2_run, s2_exit, NULL, NULL)
        };
And this example creates three hierarchical states:

.. code-block:: C
        enum demo_state { S0, S1, S2 };

        const struct smf_state demo_states[] = {
        [S0] = SMF_CREATE_STATE(s0_entry, s0_run, s0_exit, parent_s0, NULL),
        [S1] = SMF_CREATE_STATE(s1_entry, s1_run, s1_exit, parent_s12, NULL),
        [S2] = SMF_CREATE_STATE(s2_entry, s2_run, s2_exit, parent_s12, NULL)
        };

This example creates three hierarchical states with an initial transition from parent state S0 to child state S2:

.. code-block:: C
        enum demo_state { S0, S1, S2 };

        /* Forward declaration of state table */
        const struct smf_state demo_states[];

        const struct smf_state demo_states[] = {
        [S0] = SMF_CREATE_STATE(s0_entry, s0_run, s0_exit, NULL, demo_states[S2]),
        [S1] = SMF_CREATE_STATE(s1_entry, s1_run, s1_exit, demo_states[S0], NULL),
        [S2] = SMF_CREATE_STATE(s2_entry, s2_run, s2_exit, demo_states[S0], NULL)
        };

---------------------------
How to Set a Initial State?
---------------------------

To set the initial state, the `smf_set_initial()` function should be called.

------------------------------------------
How to Transition from 1 State to Another?
------------------------------------------

To transition from one state to another, the `smf_set_state()` function is used.

*****************************************************
Intial Transition Must be Defined for all Child Nodes
*****************************************************

If `CONFIG_SMF_INITIAL_TRANSITION` is not set, `smf_set_initial()` and `smf_set_state()` function should not be passed a parent state as the parent state does not know which child state to transition to. Transitioning to a parent state is OK if an initial transition to a child state is defined. A well-formed HSM should have initial transitions defined for all parent states.

1. The Core Rule: You Cannot Directly Enter a "Blank" Parent
 A parent state is just an abstract container; the system cannot simply exist in a generic "Parent State." It must reside in a concrete child state inside that parent.

 The Problem: If you tell the framework to go to a parent state (ParentA), but ParentA doesn't know which child state is the default starting point, the framework will crash or exhibit undefined behavior.

 The Solution: The parent state needs an initial transition (a default pointer that says, "When someone enters me, immediately redirect them to ChildState1").

2. The Kconfig Option: `CONFIG_SMF_INITIAL_TRANSITION`
 Zephyr provides a configuration option called `CONFIG_SMF_INITIAL_TRANSITION` that enables or disables this automatic redirection feature.

 Case A: If `CONFIG_SMF_INITIAL_TRANSITION` is DISABLED
        If this config is turned off, the framework loses the ability to automatically handle initial child redirections.

        The Restriction: You are strictly forbidden from passing a parent state into smf_set_initial() or smf_set_state().

        What you must do instead: You must explicitly target the exact, granular child state. Instead of saying smf_set_state(smf, &parent_state), you must say smf_set_state(smf, &child_state_1).

 Case B: If `CONFIG_SMF_INITIAL_TRANSITION` is ENABLED
        If this config is turned on, the framework allows you to target a parent state, provided you have properly defined the parent's initial transition.

        What happens: When you call smf_set_state(smf, &parent_state), the framework enters the parent, sees the initial transition pointer, and automatically routes the system down into the designated child state.

Even if the framework allows you to bypass it by targeting children directly, good architectural design dictates that every parent state should explicitly define its own default starting child state.

*****************************************************
Why not Transition to another State in EXIT Function?
*****************************************************

While the state machine is running, `smf_set_state()` should only be called from the Entry or Run function. Calling `smf_set_state()` from Exit functions will generate a warning in the log and no transition will occur.

1. The Timeline of a Transition

 To see why smf_set_state() fails in an Exit function, look at what Zephyr actually does behind the scenes when a transition occurs:

 [State A: Run] ──(smf_set_state called)──> [State A: Exit] ──> [State B: Entry]

 You are in State A's Run function. An event happens, and you decide to change states. You call `smf_set_state(..., &State_B)`.

 The framework acknowledges this and says, "Okay, we need to leave State A." It immediately executes State A's Exit function to clean up resources (turn off timers, free memory).

 The framework then executes State B's Entry function to initialize the new state.

1. The Danger: Why Exit Cannot Trigger a Transition

 Imagine what happens if Zephyr allowed you to call `smf_set_state()` inside an Exit function.

 If you are already inside State A's Exit function, and you suddenly call `smf_set_state(..., &State_C)`, you are trying to trigger a new transition while the previous transition hasn't even finished yet.

 This causes two major issues:

        a. **Infinite Recursion / Call Stack Overflow**: The framework would have to interrupt the current Exit function to call another Exit function, which might call another smf_set_state(), rapidly consuming the microcontroller's tiny RAM stack until the system crashes.

        b. **The "Where was I going?" Paradox**: The state machine was already mid-flight to State B. By changing the destination to State C while halfway out the door, the internal pointers of the framework get corrupted.

 Because of this, Zephyr strictly treats the Exit function as read-only cleanup code. It is meant for shutting things down, not for making executive decisions about the future.

`````````````````````````````

State Machine Execution
To run the state machine, the smf_run_state() function should be called in some application dependent way. An application should cease calling smf_run_state if it returns a non-zero value.

State Machine Termination
To terminate the state machine, the smf_set_terminate() function should be called. It can be called from the entry, run, or exit actions. The function takes a non-zero user defined value that will be returned by the smf_run_state() function.

Retrieving the Current State
Leaf State: In the context of a hierarchical state machine, a leaf state is a state that does not contain any child states. It represents the most granular level of state in the hierarchy, where no further decomposition is possible.

Executing State: The executing state refers to the state whose entry, run, or exit action is currently being executed by the state machine. This may be a parent or leaf state, depending on the current operation.

To retrieve the current leaf state, the smf_get_current_leaf_state() function should be called. For example:

const struct smf_state *leaf_state = smf_get_current_leaf_state(SMF_CTX(&s_obj));
Note

If `CONFIG_SMF_INITIAL_TRANSITION` is not enabled, or if the initial state of a parent state is not defined, always set the state to a leaf state. Otherwise, the state machine may enter a parent state directly, and smf_get_current_leaf_state() may return a parent state instead of a leaf state. Ensure initial transitions are properly configured for all parent states to avoid malformed hierarchical state machines.

To retrieve the state whose entry, run, or exit action is currently being executed, use the smf_get_current_executing_state() function.

UML State Machines
SMF follows UML hierarchical state machine rules for transitions i.e., the entry and exit actions of the least common ancestor are not executed on transition, unless said transition is a transition to self.

The UML Specification for StateMachines may be found in chapter 14 of the UML specification available here: https://www.omg.org/spec/UML/

SMF breaks from UML rules in:

Executing the actions associated with the transition within the context of the source state, rather than after the exit actions are performed.

Only allowing external transitions to self, not to sub-states. A transition from a superstate to a child state is treated as a local transition.

Prohibiting transitions using smf_set_state() in exit actions.

SMF also does not provide any pseudostates except the Initial Pseudostate. Terminate pseudostates can be modelled by calling smf_set_terminate() from the entry action of a ‘terminate’ state. Orthogonal regions are modelled by calling smf_run_state() for each region.

State Machine Examples
Flat State Machine Example
This example turns the following state diagram into code using the SMF, where the initial state is S0.


Flat state machine diagram

Code:

#include <zephyr/smf.h>

/* Forward declaration of state table */
static const struct smf_state demo_states[];

/* List of demo states */
enum demo_state { S0, S1, S2 };

/* User defined object */
struct s_object {
        /* This must be first */
        struct smf_ctx ctx;

        /* Other state specific data add here */
} s_obj;

/* State S0 */
static void s0_entry(void *o)
{
        /* Do something */
}
static enum smf_state_result s0_run(void *o)
{
        smf_set_state(SMF_CTX(&s_obj), &demo_states[S1]);
        return SMF_EVENT_HANDLED;
}
static void s0_exit(void *o)
{
        /* Do something */
}

/* State S1 */
static enum smf_state_result s1_run(void *o)
{
        smf_set_state(SMF_CTX(&s_obj), &demo_states[S2]);
        return SMF_EVENT_HANDLED;
}
static void s1_exit(void *o)
{
        /* Do something */
}

/* State S2 */
static void s2_entry(void *o)
{
        /* Do something */
}
static enum smf_state_result s2_run(void *o)
{
        smf_set_state(SMF_CTX(&s_obj), &demo_states[S0]);
        return SMF_EVENT_HANDLED;
}

/* Populate state table */
static const struct smf_state demo_states[] = {
        [S0] = SMF_CREATE_STATE(s0_entry, s0_run, s0_exit, NULL, NULL),
        /* State S1 does not have an entry action */
        [S1] = SMF_CREATE_STATE(NULL, s1_run, s1_exit, NULL, NULL),
        /* State S2 does not have an exit action */
        [S2] = SMF_CREATE_STATE(s2_entry, s2_run, NULL, NULL, NULL),
};

int main(void)
{
        int32_t ret;

        /* Set initial state */
        smf_set_initial(SMF_CTX(&s_obj), &demo_states[S0]);

        /* Run the state machine */
        while(1) {
                /* State machine terminates if a non-zero value is returned */
                ret = smf_run_state(SMF_CTX(&s_obj));
                if (ret) {
                        /* handle return code and terminate state machine */
                        break;
                }
                k_msleep(1000);
        }
}
Hierarchical State Machine Example
This example turns the following state diagram into code using the SMF, where S0 and S1 share a parent state and S0 is the initial state.


Hierarchical state machine diagram

Code:

#include <zephyr/smf.h>

/* Forward declaration of state table */
static const struct smf_state demo_states[];

/* List of demo states */
enum demo_state { PARENT, S0, S1, S2 };

/* User defined object */
struct s_object {
        /* This must be first */
        struct smf_ctx ctx;

        /* Other state specific data add here */
} s_obj;

/* Parent State */
static void parent_entry(void *o)
{
        /* Do something */
}
static void parent_exit(void *o)
{
        /* Do something */
}

/* State S0 */
static enum smf_state_result s0_run(void *o)
{
        smf_set_state(SMF_CTX(&s_obj), &demo_states[S1]);
        return SMF_EVENT_HANDLED;
}

/* State S1 */
static enum smf_state_result s1_run(void *o)
{
        smf_set_state(SMF_CTX(&s_obj), &demo_states[S2]);
        return SMF_EVENT_HANDLED;
}

/* State S2 */
static enum smf_state_result s2_run(void *o)
{
        smf_set_state(SMF_CTX(&s_obj), &demo_states[S0]);
        return SMF_EVENT_HANDLED;
}

/* Populate state table */
static const struct smf_state demo_states[] = {
        /* Parent state does not have a run action */
        [PARENT] = SMF_CREATE_STATE(parent_entry, NULL, parent_exit, NULL, NULL),
        /* Child states do not have entry or exit actions */
        [S0] = SMF_CREATE_STATE(NULL, s0_run, NULL, &demo_states[PARENT], NULL),
        [S1] = SMF_CREATE_STATE(NULL, s1_run, NULL, &demo_states[PARENT], NULL),
        /* State S2 do not have entry or exit actions and no parent */
        [S2] = SMF_CREATE_STATE(NULL, s2_run, NULL, NULL, NULL),
};

int main(void)
{
        int32_t ret;

        /* Set initial state */
        smf_set_initial(SMF_CTX(&s_obj), &demo_states[S0]);

        /* Run the state machine */
        while(1) {
                /* State machine terminates if a non-zero value is returned */
                ret = smf_run_state(SMF_CTX(&s_obj));
                if (ret) {
                        /* handle return code and terminate state machine */
                        break;
                }
                k_msleep(1000);
        }
}
When designing hierarchical state machines, the following should be considered:

Ancestor entry actions are executed before the sibling entry actions. For example, the parent_entry function is called before the s0_entry function.

Transitioning from one sibling to another with a shared ancestry does not re-execute the ancestor's entry action or execute the exit action. For example, the parent_entry function is not called when transitioning from S0 to S1, nor is the parent_exit function called.

Ancestor exit actions are executed after the exit action of the current state. For example, the s1_exit function is called before the parent_exit function is called.

The parent_run function only executes if the child_run function does not call either smf_set_state() or return SMF_EVENT_HANDLED.

Avoid malformed hierarchical state machines by ensuring the state always transitions to a leaf state when `CONFIG_SMF_INITIAL_TRANSITION` is not enabled, or when a parent state’s initial state is undefined.

Event Driven State Machine Example
Events are not explicitly part of the State Machine Framework but an event driven state machine can be implemented using Zephyr Events.


Event driven state machine diagram

Code:

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/smf.h>

#define SW0_NODE        DT_ALIAS(sw0)

/* List of events */
#define EVENT_BTN_PRESS BIT(0)

static const struct gpio_dt_spec button =
        GPIO_DT_SPEC_GET_OR(SW0_NODE, gpios, {0});

static struct gpio_callback button_cb_data;

/* Forward declaration of state table */
static const struct smf_state demo_states[];

/* List of demo states */
enum demo_state { S0, S1 };

/* User defined object */
struct s_object {
        /* This must be first */
        struct smf_ctx ctx;

        /* Events */
        struct k_event smf_event;
        int32_t events;

        /* Other state specific data add here */
} s_obj;

/* State S0 */
static void s0_entry(void *o)
{
        printk("STATE0\n");
}

static enum smf_state_result s0_run(void *o)
{
        struct s_object *s = (struct s_object *)o;

        /* Change states on Button Press Event */
        if (s->events & EVENT_BTN_PRESS) {
                smf_set_state(SMF_CTX(&s_obj), &demo_states[S1]);
        }
        return SMF_EVENT_HANDLED;
}

/* State S1 */
static void s1_entry(void *o)
{
        printk("STATE1\n");
}

static enum smf_state_result s1_run(void *o)
{
        struct s_object *s = (struct s_object *)o;

        /* Change states on Button Press Event */
        if (s->events & EVENT_BTN_PRESS) {
                smf_set_state(SMF_CTX(&s_obj), &demo_states[S0]);
        }
        return SMF_EVENT_HANDLED;
}

/* Populate state table */
static const struct smf_state demo_states[] = {
        [S0] = SMF_CREATE_STATE(s0_entry, s0_run, NULL, NULL, NULL),
        [S1] = SMF_CREATE_STATE(s1_entry, s1_run, NULL, NULL, NULL),
};

void button_pressed(const struct device *dev,
                struct gpio_callback *cb, uint32_t pins)
{
        /* Generate Button Press Event */
        k_event_post(&s_obj.smf_event, EVENT_BTN_PRESS);
}

int main(void)
{
        int ret;

        if (!gpio_is_ready_dt(&button)) {
                printk("Error: button device %s is not ready\n",
                        button.port->name);
                return;
        }

        ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
        if (ret != 0) {
                printk("Error %d: failed to configure %s pin %d\n",
                        ret, button.port->name, button.pin);
                return;
        }

        ret = gpio_pin_interrupt_configure_dt(&button,
                GPIO_INT_EDGE_TO_ACTIVE);
        if (ret != 0) {
                printk("Error %d: failed to configure interrupt on %s pin %d\n",
                        ret, button.port->name, button.pin);
                return;
        }

        gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
        gpio_add_callback(button.port, &button_cb_data);

        /* Initialize the event */
        k_event_init(&s_obj.smf_event);

        /* Set initial state */
        smf_set_initial(SMF_CTX(&s_obj), &demo_states[S0]);

        /* Run the state machine */
        while(1) {
                /* Block until an event is detected */
                s_obj.events = k_event_wait(&s_obj.smf_event,
                                EVENT_BTN_PRESS, true, K_FOREVER);

                /* State machine terminates if a non-zero value is returned */
                ret = smf_run_state(SMF_CTX(&s_obj));
                if (ret) {
                        /* handle return code and terminate state machine */
                        break;
                }
        }
}
State Machine Example With Initial Transitions And Transition To Self
tests/lib/smf/src/test_lib_self_transition_smf.c defines a state machine for testing the initial transitions and transitions to self in a parent state. The statechart for this test is below.


Test state machine for UML State Transitions

Test Instrumentation
The SMF provides optional instrumentation hooks for observing state machine behavior during testing. To enable them, set CONFIG_SMF_INSTRUMENTATION.

When enabled, three hook callbacks can be registered on a state machine context via smf_set_hooks():

on_action — called before each entry, run, or exit action executes.

on_transition — called after the current state pointer is updated but before the new state’s entry actions execute.

on_error — called when an invalid operation is detected (e.g. a NULL transition target or a transition attempted from an exit action).

Important

smf_set_hooks() must be called after smf_set_initial(), because smf_set_initial() resets the hooks pointer to NULL. As a consequence, entry actions executed during smf_set_initial() (i.e. the initial state’s entry actions and those of its ancestors) will not be captured by the hooks.

Example:

#include <zephyr/smf.h>

static void on_action(struct smf_ctx *ctx,
                      const struct smf_state *state,
                      smf_action_type action_type)
{
        /* Log or record the action */
}

static void on_transition(struct smf_ctx *ctx,
                          const struct smf_state *source,
                          const struct smf_state *dest)
{
        /* Log or record the transition */
}

static const struct smf_hooks hooks = {
        .on_action = on_action,
        .on_transition = on_transition,
        /* .on_error = NULL — any member may be NULL */
};

void test_example(void)
{
        struct s_object s_obj;

        /* Set the initial state first */
        smf_set_initial(SMF_CTX(&s_obj), &demo_states[S0]);

        /* Install hooks after init — initial entry actions are not captured */
        smf_set_hooks(SMF_CTX(&s_obj), &hooks);

        /* Run the state machine — hooks fire on every action and transition */
        while (!smf_run_state(SMF_CTX(&s_obj))) {
                /* ... */
        }
}
When CONFIG_SMF_INSTRUMENTATION is not set, all instrumentation code is compiled out and there is zero runtime overhead.