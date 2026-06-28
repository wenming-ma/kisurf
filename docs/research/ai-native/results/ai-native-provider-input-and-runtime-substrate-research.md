# AI Native Provider Input, Memory, Artifacts, and Runtime Lifecycle Architecture for KiSurf

## Executive recommendation

KiSurf should treat **the editor’s structured state and immutable event journal as the source of truth**, and treat the LLM conversation as only one consumer of that state. The literature on long-context use is clear on two points: model performance degrades before hard context-window failure, especially when crucial information sits in the middle of long prompts, and durable agent behavior improves when systems separate working memory, episodic memory, and longer-lived semantic memory instead of relying on one growing transcript. OS-inspired memory hierarchies such as MemGPT, retrieval-based memory architectures, and recency/relevance/reflection loops all converge on the same practical conclusion: **do not let the transcript become the architecture**. citeturn0search13turn12search1turn11search0turn10view0turn7search3

For KiSurf specifically, the best long-term design is a **hybrid, event-sourced, hierarchical memory system**: immutable raw journals at the bottom; derived editor state and versioned board/schematic snapshots above that; task- and episode-level summaries in the middle; and compact, per-call assembled provider input at the top. Pure transcript truncation is too lossy for design intent and constraint recall; rolling summaries alone risk irreversible omission; retrieval-only memory over raw logs is better, but still brittle without typed state, provenance, and lifecycle rules. A hybrid design is more complex, but it matches KiSurf’s native strengths: structured editor access, validation, preview/rollback, and accept/reject workflows. citeturn6search1turn6search16turn5search0turn4search3turn14search10

A concise decision matrix is useful here:

| Option | What it gets right | What breaks in KiSurf |
|---|---|---|
| Pure transcript truncation | Very easy to build | Loses constraints, rationale, and accepted-edit history; vulnerable to lost-in-the-middle effects |
| Rolling summaries only | Controls size cheaply | Early summarization errors become durable; poor provenance |
| Hierarchical memory | Separates working, episodic, semantic memory | Needs governance and memory maintenance |
| Retrieval over raw journals | Good recall and provenance | Ranking can miss latent constraints without strong structure |
| Event-sourced hybrid | Best fit for auditability, rollback, previews, and editor state | Highest implementation complexity |

This recommendation is supported by event-sourcing practice, RAG research, prompt-compression work, long-context studies, and recent memory-system work emphasizing provenance, maintenance, and governed retrieval. citeturn6search1turn5search0turn4search0turn4search2turn14search6

## Recommended memory hierarchy for an AI-native EDA editor

Two patterns from the research map unusually well onto KiSurf: an **OS-like memory hierarchy** and an **observe → retrieve → reflect → plan/act** loop. MemGPT explicitly frames this as virtual context management across memory tiers, while Generative Agents shows how a memory stream, retrieval, reflection, and planning loop can maintain coherence over time. KiSurf should adapt those patterns, but ground them in editor state rather than natural-language diary entries. citeturn11search0turn11search5turn10view0

iturn15image0

The right hierarchy for KiSurf is not just “short-term vs long-term.” It is a stack of **authoritative, typed layers** with different write and read policies.

| Layer | Scope | Durability | Primary writer | Primary use at inference time |
|---|---|---|---|---|
| Immediate provider input | Per call | Ephemeral | Provider input compiler | Current task execution |
| Live editor state | Current board/schematic/view | Derived, always current | Core editor | Ground truth for geometry, selection, active tool, viewport |
| Chat session transcript | Per chat | Archived | User + Chat Agent | Recent conversational continuity only |
| AI execution journal | Per execution session | Short/medium | Agent runtime | Tool lineage, previews, validation chain, rollback state |
| Next Action episode context | Per background episode | Short-lived | Next Action runtime | Local suggestion generation |
| Recent user activity timeline | Project-local | Medium | Editor event stream | What changed, in what order, by whom/what |
| Project persistent memory | Project-wide | Durable | Consolidation jobs + acceptance pipeline | Stable conventions and accepted prior decisions |
| Design intent and constraints memory | Project-wide | Durable, high-trust | Human/user + validated extraction | “Hard constraints” block for future calls |
| Accepted-edit summaries | Project-wide | Durable | Consolidator | Retrieval of prior rationale and outcomes |
| Tool/validation memory | Project-wide | Durable but typed | Tool result normalizers | Failure patterns, prior DRC/ERC context, known limits |

This layering mirrors the now-common distinction in agent-memory work between short-term working memory, episodic experience memory, and semantic or profile-like durable memory. It also matches the emerging practical decomposition of agent memory into representation/storage, extraction, retrieval/routing, and maintenance. citeturn18search8turn13search1turn14search6turn7academia21

The most important architectural choice is that **the immutable journal sits below all summaries**. Event sourcing stores state changes as events and allows reconstruction of prior states; that is exactly what KiSurf needs for preview branches, validation before/after comparisons, accept/reject records, and rollback auditability. Periodic snapshots can accelerate reconstruction, but they must remain derived from the event log rather than replacing it. citeturn6search1turn6search3turn6search16

For KiSurf, I recommend the following durable memory classes:

| Durable class | What belongs here | Write gate |
|---|---|---|
| Hard design constraints | Net classes, placement exclusions, impedance or clearance intent, board outline rules, manufacturing constraints | Human-entered or tool-verified only |
| Stable project conventions | Naming conventions, preferred footprint/vendor patterns, annotation style, board-specific routing preferences | Repeated acceptance or explicit user confirmation |
| Accepted-edit rationale | Why a placement/routing/refactor was accepted and what validation it passed | Accept event + structured summary |
| Known failure memory | Persistent tool limitations, repeated DRC pitfalls, project-specific gotchas | Verified recurrence, not a single failed attempt |
| User preference memory | Interaction preferences that affect agent behavior but not board correctness | Explicit opt-in or repeated stable evidence |

Everything else should be harder to promote. That includes raw transcripts, hidden failed attempts, speculative previews, and noisy tool blobs. Recent work on provenance-grounded memory and provenance-role collapse argues that agents become unreliable when evidence, extracted beliefs, and action-driving summaries collapse into one undifferentiated text pool. KiSurf should therefore store memory as **typed records with provenance and trust level**, not as free-form narrative alone. citeturn14search1turn14search5turn14search12

## How context should be compiled for each LLM call

The best practice is to compile context **from policy, live state, and retrieved evidence**, not by replaying the whole conversation. Long-context studies show that more context is not automatically better, and Anthropic’s current documentation explicitly warns that recall can degrade as token count grows, calling this “context rot.” The “lost in the middle” results also show that relevant information buried in the center of long inputs is less likely to be used correctly. citeturn12search1turn0search13turn4search2

That leads to a concrete KiSurf policy: every LLM call should be assembled from a **budgeted working set**.

```text
[cacheable system + tool schema + project constraints]
[task frame: what the agent is trying to do right now]
[live editor state digest]
[current session/episode digest]
[retrieved accepted memories and relevant journal excerpts]
[very recent user-visible turns only]
[active previews / validation deltas / unresolved blockers]
[reserved headroom for model output and follow-up tool use]
```

This order is deliberate. Critical constraints should appear early in a compact, typed block, while the most relevant retrieved evidence should also be placed late enough to avoid being buried in the middle. That placement strategy is an inference from long-context findings and prompt-compression work showing that dense, question-aware context beats undifferentiated bulk context. citeturn0search13turn4search2turn4search8

### Token budgeting

Providers now expose large windows, but those windows are not a license to be sloppy. Anthropic’s docs note that all input and output components count toward the context window, including tool-related content, and also provide preflight token-counting APIs. OpenAI’s current API platform similarly exposes context-management features such as token counting and compaction, and its public API page lists frontier models with context lengths ranging from hundreds of thousands to about one million tokens depending on model class. citeturn8search8turn8search4turn8search6turn17search0turn2search5

For KiSurf, a good starting rule is:

| Agent | Recommended input target | Recommended reserve |
|---|---|---|
| Chat Agent | Use only about 60–70% of the provider window for assembled input | Keep 30–40% for output, tool calls, validation echoes, and safety margin |
| Next Action Agent | Use only about 25–40% of the provider window | Keep the rest as margin because background episodes should be small and fast |

Within the input target, KiSurf should allocate approximately:

| Input slice | Chat Agent | Next Action Agent |
|---|---:|---:|
| Cacheable instructions, tool schema, project constraints | 15% | 15% |
| Live editor state and local geometry digest | 20% | 35% |
| Session/episode digest | 10% | 15% |
| Retrieved episodic/project memory | 25% | 15% |
| Recent chat or recent user activity | 15% | 10% |
| Validation/preview deltas | 10% | 10% |
| Visual inputs | 5% by default | 0–10% only when spatial ambiguity requires it |

These percentages are a recommended operating policy rather than a provider mandate, but they are grounded in current token-counting and context-window guidance, plus evidence that focused retrieval and compression outperform indiscriminate long prompts. citeturn8search4turn16search0turn0search13turn4search0turn4search2

### Retrieval, summarization, and compression

KiSurf should retrieve from **three indexes at once**: a structured index over typed memory fields, a lexical index for precise terms like refdes/nets/constraint names, and a semantic/vector index over episode summaries and selected raw journal spans. Classic RAG exists because parametric memory alone is not enough for knowledge-intensive tasks, and recent memory work shows that retrieval quality improves when the system preserves raw episodic evidence and expands around nucleus matches rather than storing only aggressively extracted facts. citeturn5search0turn13search1turn13search2

Summaries should be layered:

| Summary type | Trigger | Lifetime | Used for |
|---|---|---|---|
| Turn digest | After each tool/result cluster | Short | Compress noisy tool chains |
| Session digest | Every N turns or at idle boundary | Medium | Replace transcript replay |
| Episode digest | On accept/reject/expire | Medium/Durable | Retrieval and anti-repetition |
| Project consolidation summary | Periodic or milestone-based | Durable | Stable design memory |

A key lesson from provenance-aware memory research is that **summaries should not erase the raw episode**. Store the summary, but keep references back to the source journal IDs, board-state hashes, tool-result hashes, preview IDs, and validation IDs. That lets KiSurf escalate from the summary tier to the raw tier when a new task depends on details that the summary omitted. citeturn14search10turn14search5turn6search16

### Tool outputs and visual frames

KiSurf’s tools should return **typed, normalized artifacts first**. ReAct and Toolformer both reinforce the value of external tools, but in a production editor the most important optimization is not merely tool availability; it is preventing tool chatter from ballooning context. Tool outputs should therefore be normalized into structured deltas, hashes, counts, status codes, and short natural-language abstracts, with raw stdout/stderr or large JSON stored out-of-band and attached by reference. citeturn3search0turn3search3turn16search4

Visual inputs should be included only when structured state is insufficient. Current provider docs make clear that images consume tokens, and many-image requests can hit stricter limits. In KiSurf, images are most justified when the task is inherently spatial or appearance-sensitive: dense placement, silk readability, local routing congestion, annotation crowding, footprint orientation ambiguity, or any “this area here” user reference that cannot be grounded well enough from the editor’s object graph alone. Use at most one current frame and, when necessary, one comparison frame, both tagged with board hash, viewport, layer set, and selection metadata. citeturn8search0turn12search14turn8search8

### Provider-limit and provider-failure handling

KiSurf should treat context management as a first-class reliability layer, not prompt hygiene. Anthropic now documents token counting before send, server-side compaction for long-running conversations, explicit stop reasons such as `model_context_window_exceeded`, and request-size failures such as `413 request_too_large`. The practical implication is straightforward: **never retry a large failed request unchanged**. citeturn8search4turn16search0turn12search3turn12search6

The retry ladder should be deterministic:

| Step | Action |
|---|---|
| Preflight | Count tokens and estimate tool/image contribution before every call |
| If over soft budget | Remove verbose tool blobs, replace old transcript spans with session summary, reduce retrieval depth |
| If still over | Drop visual inputs unless essential, compress retrieved episode texts, keep only the last few visible turns |
| If still over | Switch to a larger-window provider/model if allowed by policy |
| On provider truncation or context stop | Treat partial answer as incomplete; reissue with smaller context and explicit continuation strategy |
| On 5xx after large input | Shrink context first, then retry with idempotency and preserved request trace |

This is the right place for a **Provider Context Guard** in the KiSurf core.

## Chat Agent sessions and New Chat semantics

For the Chat Agent, “New Chat” should mean **a new visible transcript and a new model conversation boundary**, not merely clearing the UI. If KiSurf only clears the visible transcript while keeping the same hidden model context, it will preserve the exact failure mode that caused the earlier provider incidents: invisible, unbounded carry-over. At the same time, “New Chat” should not reset the project, editor state, or durable accepted memory, because those are not properties of the conversation; they are properties of the design environment. This separation between working memory and durable memory is aligned with hierarchical memory thinking and with current provider support for conversation compaction instead of unbounded replay. citeturn11search0turn16search0turn17search0

The clean semantic model is:

| On New Chat | Result |
|---|---|
| Visible transcript | Start empty |
| Model conversation/session ID | New ID |
| Current board/schematic/editor state | Preserve |
| Project persistent memory | Preserve |
| Design-intent/constraint memory | Preserve |
| Accepted-edit summaries | Preserve |
| Active execution session journal | Close the old one; start a new empty execution journal |
| Pending background Next Action episodes | Preserve only if editor state still warrants them; do not import into chat context automatically |
| Pending chat-generated previews | Freeze as review items or force explicit resolution; do not silently inherit them into the new chat |
| Undo/redo stack | Preserve, because it belongs to editor state, not the chat |
| Tool/result raw blobs from prior chat | Archive, not in working context unless retrieved |

The important nuance is the preview and journal behavior. Because KiSurf has preview, rollback, and accept/reject semantics, a chat boundary should also be an **execution boundary**. If a chat session is in the middle of a multi-step edit plan, New Chat should not silently continue that plan. Either the system should require explicit resolve/cancel, or it should freeze the preview into a reviewable artifact outside the new session. This is the same principle that event-sourced systems use to maintain auditability across state transitions. citeturn6search1turn6search7

Chat memory carry-over should be selective. Raw old messages can remain archived for the user’s benefit, but only three things should cross the session boundary by default: stable project memory, hard design constraints, and accepted-edit summaries with provenance. Rejected previews, speculative reasoning, and unaccepted “maybe this” tool attempts should not become the seed of the next chat. Work on provenance collapse and memory poisoning is directly relevant here: if low-trust or speculative content can quietly migrate into durable memory, future sessions will act on fiction with the authority of project history. citeturn14search1turn13search8turn14search12

A small but important UI implication follows: the user-visible transcript and the model-facing compiled context are different objects. KiSurf should expose a “context sent to model” debug trace or expandable audit view so that engineers can inspect why the agent knew something in the new chat even though it was not visible in the current transcript. Emerging provenance work argues that opaque prompt paths make failures hard to diagnose; KiSurf should avoid that trap from the beginning. citeturn14search5turn14search7

## Next Action episodes and cross-agent sharing

The Next Action Agent should **not** have a single continuous background memory stream equivalent to the Chat Agent. Its job is local, proactive, and state-derived. Research on event segmentation and episodic memory strongly suggests that better performance comes from segmenting continuous interaction into coherent events or episodes rather than storing all history as a flat sequence of turns. That is especially true in KiSurf, where the semantic unit is rarely “a message”; it is more often “a placement tweak,” “a routing burst on net N,” “a DRC cleanup loop,” or “a focus shift to power input decoupling.” citeturn18search3turn18search10turn18search1

So the right model is **short-lived episodes keyed to semantic work scope**. An episode should usually be opened by one of the following boundaries:

| Episode trigger | Example |
|---|---|
| Active tool transition | Move from placement to interactive routing |
| Focus-region change | User pans/zooms/selects a different functional area |
| Selection semantic change | Switch from a regulator cluster to USB ESD devices |
| Validation event | DRC/ERC issue appears or is resolved |
| Commit boundary | User accepts/rejects a preview or completes an edit burst |
| Surprise/conflict boundary | Large board-state delta or contradiction with current suggestion |

This resembles the move from rigid turn chunks to semantically coherent event segments in ES-Mem, and it fits KiSurf far better than a time-based polling stream. citeturn18search3

Each Next Action episode should carry only a compact local frame:

| Episode context field | Description |
|---|---|
| Episode ID and start cause | Why this suggestion loop exists |
| Board/schematic state hash | Version guard |
| Local work scope | Objects, nets, region, sheet, layer set |
| Recent local activity window | Last meaningful user and agent events in scope |
| Current validation delta | Relevant DRC/ERC issues or nearby warnings |
| Candidate suggestion history | Current suggestion plus superseded/review state |
| Retrieved project memory | Only local constraints/conventions relevant to this scope |

That makes the agent mostly state-derived, with small episodic recall. The agent can still use an observe → act → render/validate → review → publish loop, as in ReAct-like external action frameworks and Generative Agents-style memory loops, but it should remain **episodic and bounded**. citeturn3search0turn10view0

Expiry and supersession rules should be explicit:

| Condition | Episode outcome |
|---|---|
| Board hash changed outside expected branch | Expire |
| User changed active tool or work scope materially | Supersede |
| Newer suggestion targets the same scope | Older suggestion superseded |
| Time-to-live elapsed with no interaction | Expire |
| User explicitly rejected suggestion | Close and create short anti-repeat trace |
| Suggestion accepted and applied | Close and promote accepted summary |

That matters because the point of Next Action is timeliness, not long-horizon persona continuity. A stale background suggestion is worse than no suggestion. citeturn10view0turn18search3

### Safe sharing between Chat Agent and Next Action Agent

The two agents should share **project memory**, not **working memory**. Shared items should be durable, high-trust, and scoped by project or design region. Isolated items should stay local to the originating agent and session.

| Shared across agents | Isolated by default |
|---|---|
| Hard design constraints | Raw chat transcript |
| Stable project conventions | Hidden attempts / scratch tool chains |
| Accepted-edit summaries | Rejected or expired previews |
| Validated DRC/ERC baselines and issue classes | Chat-only private conversational context |
| Explicit user preferences about workflow | Unaccepted speculation or tentative rationale |
| Proven high-confidence tool facts | Episode-local temporary anti-loop markers |

Emerging governed shared-memory research argues that multi-agent systems need scoped retrieval, temporal supersession, provenance tracking, and policy-governed propagation. For KiSurf, that means a memory item should not become cross-agent visible simply because it exists; it should become visible only when its scope, trust level, and lifecycle policy allow it. citeturn14search0turn14search2

Rejected and expired items deserve special handling. They should remain as **ephemeral traces** for short-term anti-repetition and UX debugging, but they should not become durable project memory. Recent security work on long-term memory warns that low-trust writes can later steer consequential actions, and provenance surveys recommend explicit source metadata and write validation before promotion. citeturn13search8turn14search12

## Retention, summarization, and discard policy

The easiest way to keep KiSurf reliable is to make retention policy **type-specific** rather than global. Memory operations research increasingly frames memory as consolidation, updating, indexing, forgetting, retrieval, and compression; that is exactly the right lens here. Different artifact types should move through different retention paths. citeturn7academia21

| Artifact type | Store raw? | Summarize? | Promote to durable memory? | Typical retrieval path |
|---|---|---|---|---|
| Raw chat messages | Yes, archived by chat | Yes, session digest | Rarely | Recent-window + session summary |
| Tool calls | Yes, structured | Yes | Sometimes, if accepted/important | Execution journal + typed retrieval |
| Tool results | Raw if compact; otherwise blob by hash | Yes, always | Only verified facts | Structured result index |
| Rendered visual snapshots | Keyframes only | Optional caption/metadata | Only if tied to accepted edit or issue | Scope-local visual retrieval |
| Validation/DRC results | Yes, normalized | Yes, diff vs prior state | Yes, if persistent or accepted fix linked | Validation index |
| Hidden attempts | Prefer short TTL only | Maybe brief anti-loop note | No | Debug trace only |
| Accepted previews | Yes, patch/branch ref | Yes | Yes | Accepted-edit episode retrieval |
| Rejected/expired previews | Minimal trace only | Yes, one-line reason | No | Short anti-repeat memory |
| User activity events | Yes, raw journal | Yes, event segments | No as raw; yes as episode summaries | Activity timeline retrieval |
| Board-state hashes / context versions | Yes | No | Yes as identifiers | Version guards for every call |
| Long Python/script output | Blob + hash | Yes, extracted facts only | Almost never raw | Reference by hash + summary |

This policy is not arbitrary. It follows from three strong findings in the research and docs: raw history is useful for provenance but too expensive for routine inference; retrieval should prefer relevant reduced representations while preserving a path back to source evidence; and uncontrolled long-context accumulation causes both quality degradation and operational failures. citeturn13search1turn14search10turn12search1turn0search13

For visual memory, the retention unit should be **keyframes**, not full video-like frame histories. Store a snapshot when one of four things happens: a user references a region visually, the agent proposes a preview that changes the local appearance/layout, validation highlights a visible issue, or an accepted edit meaningfully changes the local geometry. Attach viewport, layer visibility, board hash, and selection set. Everything else can be a short-lived cache. Current multimodal-memory research points in the same direction: visual memory is useful, but best when organized around events and objects rather than indiscriminate continuous accumulation. citeturn8search0turn18search0turn18search5

For long script outputs, the right policy is especially strict. Store the complete raw output out-of-band, but feed back only a compact digest plus extracted structured facts such as created files, net/object counts changed, warnings, exceptions, and validation deltas. Provider docs explicitly note that tool use and tool results increase token consumption, so KiSurf should never replay long stdout/stderr in future prompts unless a debugging episode explicitly requests it. citeturn16search4turn8search8

## KiSurf core architecture, implementation phases, and test strategy

The C++ core should implement context and memory as first-class subsystems. A practical component layout would look like this:

```text
Editor Core
  ├─ Event Journal
  ├─ State Snapshot Service
  ├─ Preview / Branch Manager
  ├─ Validation Service
  ├─ Session Manager
  ├─ Next Action Episode Manager
  ├─ Memory Store
  │    ├─ Raw journal store
  │    ├─ Typed durable memory store
  │    ├─ Blob store
  │    └─ Summary store
  ├─ Retrieval Index
  │    ├─ Structured index
  │    ├─ Lexical index
  │    └─ Semantic/vector index
  ├─ Summary Generator
  ├─ Provider Input Compiler
  ├─ Token Budget Manager
  ├─ Provider Context Guard
  ├─ Provider Adapter Registry
  └─ Observability / Prompt Trace Store
```

This layout reflects the now-common separation between memory storage, extraction/consolidation, retrieval/routing, and maintenance, while also honoring event-sourcing requirements for replayable design state. citeturn14search6turn6search1

The most important concrete classes and responsibilities are these:

| Component | Responsibility |
|---|---|
| `EventJournal` | Immutable append-only log for user actions, agent actions, previews, accept/reject, validation, tool calls |
| `StateVersionService` | Produces board/schematic hashes, selection hashes, viewport hashes, branch IDs |
| `ConversationSessionManager` | Owns chat boundaries, session summaries, execution journal closure on New Chat |
| `NextActionEpisodeManager` | Starts, expires, supersedes, and publishes short-lived local episodes |
| `MemoryStore` | Typed storage with scope, provenance, confidence, durability, expiry |
| `SummaryGenerator` | Generates turn/session/episode/project summaries from journal spans |
| `RetrievalIndex` | Hybrid ranking across structured predicates, lexical match, semantic similarity |
| `ContextCompiler` | Builds the model-facing prompt from policy, state, retrieval, and budget |
| `TokenBudgetManager` | Counts tokens preflight and allocates budget slices |
| `ProviderContextGuard` | Enforces model-specific limits, fallback ladder, and retry shrinkage |
| `PromptTraceStore` | Records exactly what context was sent, with sent/not-sent reasons |

The **PromptTraceStore** is worth emphasizing. One of the hardest production failures in long-lived agents is “why did the model think that?” KiSurf should log the assembled prompt graph, including which memories were retrieved, which were dropped for budget, which images were included, which summaries replaced raw spans, and which board hash/version was in force. That is essential for debugging both provider-limit incidents and design-quality regressions. citeturn14search5turn14search7

### Implementation phases

A staged rollout will keep risk manageable.

| Phase | Goal | Deliverables |
|---|---|---|
| Foundation | Stop silent context blowups now | Token counting, hard budgets, tool-output normalization, prompt trace logging, transcript cap |
| Sessionization | Make boundaries explicit | New Chat semantics, execution journal objects, chat/session summaries, board-hash guards |
| Hierarchical memory | Add durable recall without transcript replay | Typed memory store, accepted-edit summaries, constraint memory, hybrid retrieval |
| Next Action episodes | Localize proactive suggestions | Episode manager, expiry/supersede rules, local activity segmentation |
| Governance and hardening | Prevent contamination and leakage | Provenance tags, trust levels, cross-agent scope rules, promotion gates |
| Optimization | Improve latency/cost | Provider caching abstraction, summary maintenance jobs, compaction hooks, retrieval reranking |

This progression follows the practical lesson in provider docs and memory research alike: first make context bounded and observable, then make it smart. citeturn8search4turn16search0turn14search6

### Risks and edge cases

The main risks are not just operational; they are epistemic. Lossy summaries can drop decisive constraints. Undo/redo can invalidate derived memory if board hashes are not checked. A rejected preview can contaminate future behavior if it is promoted too aggressively. Cross-agent sharing can leak chat-local or low-trust content into the background agent. Visual snapshots can become stale if detached from version metadata. And a system that stores only summaries can make failures impossible to audit. All of those risks are visible in current work on provenance collapse, governed shared memory, and memory poisoning. citeturn14search1turn14search0turn13search8

### Test strategy

KiSurf should build a memory-and-context test harness, not just prompt tests.

| Test family | What to verify |
|---|---|
| Long-session replay tests | Quality and latency over hundreds/thousands of events |
| Lost-in-the-middle tests | Critical constraints survive placement in different context positions |
| Budget regression tests | Same task under shrinking/expanding budgets produces sensible degradation |
| Provider error injection | 413/stop-reason/5xx handling shrinks context deterministically |
| New Chat boundary tests | No hidden chat inheritance; durable project memory still available |
| Preview lifecycle tests | Accept/reject/rollback behavior does not leak speculative state |
| Next Action expiry tests | Suggestions expire or supersede correctly on scope/state changes |
| Provenance tests | Every durable memory item links back to source journal spans and versions |
| Anti-poisoning tests | Rejected or low-trust content does not become cross-session or cross-agent guidance |
| Observability golden tests | Prompt trace fully explains sent context and omitted items |

A particularly important benchmark for KiSurf is a **journal-to-provider-input replay suite**: take real recorded editing sessions, re-run the provider input compiler at many points, and verify that the assembled provider input contains the right live state, the right accepted prior decisions, and none of the wrong cross-session carry-over. That kind of replay harness is the natural complement to an event-sourced architecture and is precisely how KiSurf can turn memory from a hidden liability into a debuggable subsystem. citeturn6search1turn6search16turn14search5

In consequence, the recommended long-term architecture for KiSurf is clear: **event-sourced raw history, typed durable memory, per-agent working-memory isolation, retrieval-augmented context assembly, explicit session and episode boundaries, deterministic token budgeting, and provenance-rich observability**. That combination is much better aligned with both the current state of long-context model behavior and the demanding correctness requirements of an AI-native EDA editor than any transcript-first approach. citeturn0search13turn11search0turn5search0turn14search10turn14search6
