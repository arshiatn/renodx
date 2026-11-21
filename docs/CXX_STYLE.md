# RenoDX C++ style guide

This guide applies to RenoDX-authored C++ under `src/`. Vendored code under
`external/` follows its upstream project. Existing code is not required to be
rewritten solely to match this guide; apply it to new code and touched regions.

The rules below formalize conventions already present in committed RenoDX code
and the constraints imposed by independent ReShade add-ons sharing process
state.

## Formatting

- Format C++ with the repository `.clang-format`, based on Google style with
  two-space indentation and C++20 syntax.
- Treat 120 columns as a readability target, not a mechanical limit. Break
  declarations, expressions, and initializers when doing so makes them easier
  to scan. Long URLs, diagnostic text, generated text, regular expressions,
  SDK signatures, and similarly indivisible content may exceed 120 columns.
- Do not rely on `clang-format` to enforce the target. The repository sets
  `ColumnLimit: 0` because an enforced limit can flatten or distort deliberate
  layout.
- Preserve the surrounding include groups. Include the direct dependencies of
  the file instead of relying on transitive includes.
- Use braces and early returns to keep nesting shallow. A single-line `if` may
  omit braces only when its body immediately transfers control, such as with
  `return`, `break`, `continue`, or `throw`. Conditional application logic,
  function calls, assignments, and other work must use braces even when the
  body is one short statement.
- For `if`/`else`, prefer a positive condition in the `if` branch and let the
  `else` branch represent its implicit false case. Avoid `if (!condition) ...
  else ...` when the branches can be ordered clearly as `if (condition) ...
  else ...`. Reading the true case first makes the branch relationship easier
  to follow. Keep a negated condition when it materially simplifies the logic
  or enables a clear early control transfer without an `else`.
- Do not create an empty statement or empty `if`/`else` branch merely to put the
  positive condition first. Omit the empty branch and use the condition that
  directly guards the actual work. Retain an intentional no-op branch only in
  the rare case where making the complete set of cases explicit materially
  improves readability; mark it with a concise `// No-op: ...` comment that
  explains why the branch is intentionally empty.
- Use the conditional (`?:`) operator only to select a value. Do not use it to
  encode logic, trigger side effects, or exploit short-circuited evaluation.
  Use `if` or `switch` for control flow.
- Do not select an object with a conditional expression and immediately invoke
  behavior on the result, such as `(condition ? first : second).Process(...)`.
  Do not chain independent operations merely to avoid an intermediate variable.
  Assign the selected value or reference to a clearly named variable, then
  perform the common operation on it. Intermediate variables do not imply extra
  runtime work; the compiler can eliminate them. Chaining is appropriate when
  an API is deliberately designed as a fluent pipeline and each chained step is
  the natural interface, not as a general substitute for readable sequential
  statements. Use explicit `if`/`else` only when the branches perform different
  behavior.
- Use explicit parentheses when the extent of a conditional expression or one
  of its branches could be visually ambiguous. For example, do not write
  `value = a ? b : c * 5;`; write either `value = (a ? b : c) * 5;` or
  `value = a ? b : (c * 5);` to state the intended grouping directly. Do not
  rely on operator-precedence knowledge where a reader could reasonably parse
  the expression differently at a glance.
- Parenthesize a conditional expression used as the value of an assignment or
  initialization: `const auto& selected = (index == target ? first : second);`.
  This visually separates assignment from the conditional expression and avoids
  dense sequences such as `a = b == c ? d : e`.
- Prefer `switch` over a long `if`/`else if`/`else` chain when one expression
  is repeatedly compared against distinct values that select unique results.
  `switch` expresses that relationship directly, evaluates the controlling
  expression once, and gives the compiler an appropriate construct to optimize.
  Keep `if` for ranges, unrelated conditions, and cases that cannot be expressed
  clearly as discrete values.

## Files and implementation boundaries

- Do not add `.inl` files. Committed RenoDX source has no `.inl` files.
- Do not use forward declarations in RenoDX-authored code merely to control
  ordering, separate a declaration from its definition, or move implementation
  out of a type. Reorder the definitions or define the function where it is
  declared instead. A forward declaration is permitted only when it is
  necessary, such as to express an unavoidable recursive dependency, and must
  have an adjacent comment explaining the specific constraint that requires it.
- Reserve `.cpp` files for binary entry points: add-ons, applications,
  command-line tools, test executables, and comparable independently compiled
  targets. Committed production code does not split reusable implementation
  into matching `.hpp` and `.cpp` files.
- Put reusable implementation in `.hpp` files. RenoDX add-ons are independent
  binaries that compose utility and mod implementations directly rather than
  linking a common RenoDX implementation library.
- Do not create matching `.hpp` and `.cpp` files for a utility or feature. Keep
  the binary-specific composition in its entry-point `.cpp` and reusable
  behavior in the appropriate `.hpp`.
- Use `.h` for C++/HLSL shared data layouts and generated or static embedded
  assets. Use `.hpp` for C++ implementation.
- Keep game entry-point implementation in its anonymous namespace. Put shared
  utilities under `renodx::utils::<feature>` and reusable mod behavior under
  `renodx::mods::<feature>`.
- Keep platform- or SDK-specific implementation behind the narrowest useful
  boundary. Game-specific hashes, resource slots, and scheduling remain in the
  game adapter rather than shared utilities.

## Naming and linkage

- Use `PascalCase` for types, type aliases, functions, and methods.
- Use `snake_case` for namespaces, files, variables, data members, and
  parameters.
- Favor clear, specific variable and type names over vague names accompanied by
  explanatory comments. Rename the value instead of documenting what an
  unclear name means. Names should communicate the value's role, ownership,
  unit, coordinate space, lifecycle state, or resource meaning where relevant.
- Use `UPPER_SNAKE_CASE` for constants and enumerators. Preserve established
  names when changing them would create unnecessary migration work.
- Use `internal` for a named private implementation namespace. Do not introduce
  a parallel `detail` convention.
- In implementation headers, use `static` for state and callbacks intentionally
  owned separately by each binary. Use `inline` where one definition is
  required by C++ linkage or where the symbol is part of the reusable header
  interface.
- Do not mistake header linkage for process-wide ownership. State that must be
  shared among independently loaded add-ons belongs in `cross_addon::Shared<T>`.
- Preserve external API names and signatures exactly even when they do not
  follow RenoDX naming conventions.

## Comments

- Clear names and straightforward control flow should make comments unnecessary
  most of the time. Do not use comments to restate clearly named code, define a
  vague variable name, or narrate individual statements.
- Use comments when they explain functionality that is not apparent locally:
  why an operation is required, which invariant it preserves, an external API
  constraint, a non-obvious lifetime or ordering requirement, or the reason
  for a compatibility workaround.
- Keep useful comments even when nearby code is refactored. Update or remove a
  comment when its explanation is no longer accurate.
- Prefer a concise explanation beside the relevant code over a long historical
  narrative. Put durable investigation history or migration plans in Markdown
  documentation instead.

## Initialization and declaration scope

- Prefer aggregate types with default member initializers. Add a constructor
  only when construction must enforce an invariant, acquire a resource, or
  perform behavior that aggregate initialization cannot express clearly.
- Heavily prefer designated initializers for aggregate construction. They make
  call sites self-documenting and prevent positional arguments from obscuring
  field meaning.
- Keep designated initializers in declaration order. This avoids compiler
  warnings and makes omitted fields apparent.
- Prefer inline declarations: declare a variable where its value first becomes
  meaningful and initialize it in the same statement. Do not collect
  uninitialized declarations at the beginning of a scope.
- Prefer clearly named intermediate variables over extremely long one-line
  expressions when the names make the operation easier to understand. Do not
  contort source code to preoptimize for the compiler; the optimizer can inline,
  fold, and eliminate intermediate values as appropriate.
- Use named intermediate variables when nested calls obscure the order of
  evaluation or make the source read in the opposite order from the work being
  performed. For example, avoid `Consume(Transform(Lookup(key)));` when each
  step represents meaningful work. Store each meaningful result in a clearly
  named variable before passing it to the next operation. Prefer code that can
  be read sequentially in execution order.
- Use initializer statements in `if`, `switch`, and range-based loops when the
  value is only meaningful to that control-flow operation.
- Keep a declaration's scope as narrow as practical. Do not retain a local
  after its last use merely to reuse a name or defer cleanup that RAII already
  handles.
- Construct return values, callback registrations, settings, and container
  entries with designated initializers at the use site when no intermediate
  mutation or reuse is needed.

## Design and reuse

- Apply DRY by keeping each behavior, invariant, policy, and piece of
  authoritative knowledge in one clear source of truth. A future change should
  have one obvious place to make it.
- Do not treat repeated or similar-looking syntax as proof that an abstraction
  is needed. Two local snippets may legitimately remain separate when they only
  happen to look alike, may evolve independently, or are clearer in place.
- Do not extract code merely because it appears twice or to make a caller
  shorter. Preserve readable, sequential control flow and local context. Small
  duplication is preferable to one-use helpers, unnecessary indirection,
  unreadable jumping between definitions, or cascading refactors.
- Do not create an empty or stateless `struct` or `class` merely to group or
  qualify functions. Use free functions in the narrowest appropriate namespace
  instead. Introduce a type only when it represents object state, enforces an
  object invariant, provides required polymorphism, or satisfies an external
  API contract.
- Do not create or retain a helper with exactly one call site. Inline it at its
  call site regardless of its length, complexity, invariant enforcement,
  domain-specific name, or internal API-boundary role. Do not justify a
  single-use helper as improving readability or hiding complexity. Externally
  required entry points, callbacks, virtual overrides, and indirectly invoked
  functions are not helpers with a single call site.
- Add a helper only when it has at least two call sites. Reuse may then be
  reinforced by invariant enforcement, hidden complexity, a stable domain
  concept, or a deliberate API boundary.
- If every call site is inside the same function or local scope, use a local
  lambda. Do not create a class-, namespace-, or file-scope helper for reuse
  confined to one enclosing function. A helper at one of those broader scopes
  requires callers in at least two distinct enclosing functions.
- Do not introduce a named local used only by the following call or return.
  Construct the value at the use site unless it is mutated, reused, or
  materially clarifies the code.
- Prefer existing RenoDX abstractions over a parallel framework or wrapper.
- Keep APIs narrow. Do not expose backend transaction state, native error
  formats, or platform implementation details when standard C++ types describe
  the contract.

## Standard C++ and platform APIs

- Prefer the C++ standard library over Microsoft-specific extensions when the
  standard library provides equivalent behavior.
- Use Win32, COM, DirectX, Detours, and vendor SDK types at the boundary that
  actually requires them; translate to standard or ReShade types at shared
  authoring boundaries.
- Prefer RAII for locks, handles, transactions, and rollback.
- Prefer `std::error_code`, standard exceptions, and standard containers over
  backend-specific equivalents in public interfaces.
- Use fixed-width integer types when width is part of storage, hashing, binary
  layout, serialization, or ABI behavior.

## Parameters, ownership, and lifetime

- Pass a non-modified, nontrivial input by `const&` when the function only
  observes or forwards it and copying is unnecessary.
- Pass an argument by pointer when the function transforms it, mutates it, or
  writes an output through it. Do not use a non-const reference for transformed
  arguments.
- Use a pointer for an optional borrowed argument, with `nullptr` representing
  absence. Avoid non-const references except where an established callback or
  external API requires one.
- Pass cheap scalar values and intentionally copied values by value.
- Make ownership explicit. Use values for copied state and smart pointers or
  documented handles for owned resources.
- Do not retain pointers, references, spans, or iterators after the lock,
  callback, container operation, or SDK lifetime that makes them valid.
- Keep attach/detach, registration/unregistration, create/destroy, and
  install/uninstall paths symmetrical.

## Concurrent state

- Destructure key/value pairs with meaningful names (`auto& [device, feature]`
  or `const auto& [device, feature]`) instead of accessing `.first` and `.second`.
  Apply this inside map callbacks as well as range-based loops.
- Use an atomic for isolated state whose value and invariant are independent.
  Do not represent one logical state as multiple atomics when an operation reads
  or writes several of them in sequence: separate atomic accesses do not provide
  a coherent snapshot or transaction. Group the related fields in a state struct
  protected by an owned `std::shared_mutex`; one lock acquisition makes the
  invariant and access boundary explicit and avoids repeated synchronization.
- Place an owned `std::shared_mutex` first in its state struct, next to the
  data it protects. Do not reorder existing UUID-backed shared ABI fields.
- Prefer a parallel hash map for shared mutable maps accessed from rendering,
  presentation, callback, or worker threads.
- Use `utils::data::ParallelFlatHashMap` or `ParallelNodeHashMap` for
  module-local state. Use `cross_addon::parallel_flat_hash_map` or
  `parallel_node_hash_map` for state shared by independently built add-ons.
- Prefer a flat map unless stable element addresses are required; use a node
  map when references or pointers must remain stable across map growth.
- Supply `std::shared_mutex` to parallel maps that have concurrent readers and
  writers. Use their callback-scoped accessors, such as `if_contains`,
  `modify_if`, `lazy_emplace_l`, and `erase_if`, rather than adding an external
  lock or returning an unsafe element pointer.
- Prefer `std::shared_mutex` for nonrecursive synchronization, including
  exclusive-only state: on the Windows/MSVC STL target it uses Win32 SRWLOCK.
  Use exclusive locks for mutations and shared locks for valid read paths.
  Exclusive-only access is not a reason to choose `std::mutex`; reserve it for
  an API contract that specifically requires it, such as `std::condition_variable`.
  Preserve recursive locks where reentrancy is required; `std::shared_mutex`
  is not recursive.
- Keep lock scope small and never invoke unknown callbacks while holding a lock
  unless the API explicitly guarantees that behavior.

## Failures and invariants

- Do not use `(void)` casts to discard results or suppress unused-variable or
  parameter warnings. Call functions directly when their result can be ignored;
  use `[[maybe_unused]]` or omit unused parameter names as appropriate. Mark
  functions `[[nodiscard]]` when callers must inspect the result, and handle
  results that are required for correct operation.
- Log the concrete failure when a required invariant or core operation fails;
  use debug output when normal logging is not yet available. A fatal error
  describes the failure of the affected operation or feature, not an automatic
  requirement to terminate the host process. Propagate a defined failure result,
  reject initialization, or stop the affected operation safely. Do not hide the
  failure with silent returns, indefinite retries, or partially initialized
  state presented to callers as usable.
- Process termination is a rare, context-specific last resort, not the default
  fatal-error policy. Use it only with a concrete justification that safe failure
  handling or containment is impossible. Logging a fatal error does not imply
  calling `std::terminate`, `abort`, or a process-exit API. Also account for
  implicit termination from escaping exceptions during global initialization or
  across `noexcept` boundaries; handle failures at an appropriate boundary.
- Assert programmer invariants and unexpected failures, including unexpected
  API failures, required initialization, valid ownership, queue identity, and
  call ordering. An operational failure is not automatically an expected outcome;
  log and assert when it violates the operation's expected contract. Do not add defensive guards for
  states excluded by the established contract merely because they can be
  imagined. Identify a reachable failure and its actual consequence before
  adding recovery or lifetime machinery.
- Trust established internal developer contracts. Prefer construction/setup
  validation or debug assertions over repeated hot-path validation of unchanged
  relationships. Cheap checks on already-available values may be appropriate,
  but low cost alone does not justify duplicated failure state or a recovery
  protocol. Do not add COM queries, allocations, locks, or wrapper discovery on
  each operation solely to re-prove a caller invariant. Diagnose a shared failure
  at its responsible boundary rather than duplicating catches and poison flags
  throughout callers without a distinct caller-owned response.
- Distinguish true fatal failures from expected operational outcomes, such as
  unsupported optional capabilities during setup. Handle an expected outcome
  explicitly only when there is a valid fallback or a clean decision not to
  enable the optional feature. Silently disabling a required core feature is
  not recovery.
- Assertions may be compiled out. Keep required operations and logging outside
  assert expressions. Pure diagnostic queries used only to check programmer
  invariants may be debug-only; their absence must not skip required work or
  change valid program behavior. Assertions diagnose programmer invariants and unexpected
  failures in builds without NDEBUG; they are compiled out when NDEBUG is defined.
  Do not assert on expected outcomes or explicitly accepted API sentinel values.
  Debug assertions are separate from unconditional process termination policy.
  In Release, keep the diagnostic and propagate the existing failure result or
  stop the affected operation; do not report success or proceed with dependent
  work. This is error propagation, not a requirement to recover, keep rendering,
  or implement a new teardown path. Expected recoverable outcomes still require
  an explicit, valid response under the pre-edit gate.
- **Fatal-failure stopping rule:** device removal/loss is fatal to rendering on
  that device. Log/assert at the responsible origin and stop or propagate failure
  through the existing boundary. That satisfies this guide's local failure
  obligation; do not attempt to salvage the removed device or require proof of
  graceful GPU retirement before accepting this response. Device recreation or
  recovery is separate work requiring an explicit request or an established
  application contract. Fatal to rendering does not mean terminate the host.
- Do not add synthetic retirement values (such as an unsignaled UINT64_MAX),
  duplicate poison flags, caller catch layers, indefinite resource retention,
  retry/drain logic, or a teardown subsystem merely to support continued execution
  after a fatal graphics failure. Do not turn an unresolved hypothetical failure
  into a recovery requirement. A failed API call alone does not prove device
  removal: preserve the actual diagnostic and classify only what the evidence
  establishes. These rules do not permit reporting pending work as complete or
  weakening normal-operation resource lifetime contracts.

## Cross-addon safety

Each RenoDX add-on is a separate module with its own C++ runtime state. Memory
allocated by one module must not be freed by another module's allocator.

- Store process-shared state through the established `cross_addon::Shared<T>`
  mechanism.
- Use `cross_addon` containers and strings for every owning object reachable
  from shared state. Do not place ordinary `std::vector`, `std::string`, or
  standard unordered containers inside shared structures.
- Treat UUID-backed shared structures as append-only ABI. Add members at the
  end; never reorder or reinterpret existing members.
- Coordinate event ownership with the existing cross-addon handler-election
  pattern so callbacks never target an unloaded module.
- Balance every consumer registration before its module unloads. Shared hook
  ownership and consumer registration are separate concerns.
- Copy state across module boundaries unless an established shared allocator
  and lifetime contract makes borrowing safe.
- Rebuild all add-ons that embed a changed UUID-backed shared structure before
  runtime validation.

## Change quality

- Keep commits scoped to one invariant or capability. Do not combine generic
  utility changes with game-specific behavior unless the utility has no
  independently buildable consumer.
- Preserve source and API compatibility unless the change explicitly includes
  a migration.
- Validate with the smallest target and focused test that exercise the changed
  utility. Broad shared-state, graphics-state, or backend changes also require
  the relevant real-API E2E path.
- Run `git diff --check` on the affected files before committing.