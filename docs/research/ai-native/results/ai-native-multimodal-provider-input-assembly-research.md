# VLM Observation and Provider Message Assembly for an AI-Native EDA Editor

## Core recommendations

A KiCad-based AI-native editor should not send a multimodal model “a screenshot plus chat history.” It should send a **compiled multimodal provider input** assembled from structured editor state, visual observation artifacts, tool results, memory, validation facts, and the required response contract. Structured editor state remains the source of truth; images are used for geometric disambiguation, visual verification, and review. That recommendation follows from three converging facts. First, KiCad already exposes actionable state such as selection, item UUIDs, positions, layer visibility, net affiliation, active layer, bounding boxes, origins, connected items, commit/rollback primitives, and render/export functions. Second, recent GUI-grounding work shows that professional high-resolution interfaces are difficult for multimodal models, especially when targets are small and the search area is large. Third, long and noisy inputs hurt model performance, and important information is used most reliably when it is placed near the beginning or end of the prompt. citeturn13view0turn13view1turn25view0turn25view1turn25view3turn26view0turn31view0turn35view0turn35view1turn35view2turn34view3turn10view2turn10view3

For KiSurf, the strongest overall design is a **shared provider-message schema** with **mode-specific assembly policies**. The shared base should always include: system policy, agent role, task objective, live editor state capsule, selected objects, viewport and layer state, recent activity, retrieved memory, validation facts, tool catalog, previous attempt summary, omitted-input trace, and an expected response schema. The differences between agents should come mostly from **scope**, **history depth**, **image count**, and **how much local geometry is made explicit as anchors and handles**. That split is consistent with GUI-agent literature that distinguishes broader planning from tighter grounding, and with work showing that short-term history improves episodic reasoning in multi-step GUI tasks. citeturn28view2turn28view3turn28view0turn9view0

I recommend the following default packet philosophy for KiSurf:

- **Chat Agent**: optimize for broad task understanding, explanation, clarification, and multi-step planning across board, schematic, recent user intent, and project memory.
- **Next Action Agent**: optimize for one-step execution in the current local episode, with strict reliance on live editor state, current mode, current ROI, and only the minimum project memory needed to avoid repeating mistakes.
- **Review agents**: never reuse actor packets verbatim. Give reviewers a compact before/after/validate packet with explicit preview provenance and decision options such as retry, rollback, publish preview, or abandon. That separation is supported by reflection-oriented GUI-agent work and by KiCad’s transaction and export primitives. citeturn9view0turn35view0turn35view1turn35view2turn31view0turn31view1turn31view2

## Evidence and constraints

KiCad provides much of the structured state that an EDA-native agent needs. On the board side, the API can return footprints, pads, tracks, vias, zones, visible layers, stackup, nets, selected items, item bounding boxes, connected items, board origins, and per-item IDs; it can also update items by internal UUID, set the active layer, refill zones, and export PDF, SVG, and raytraced renders. The editor UI also exposes an appearance panel, selection filter, message panel, and status bar, while the PCB documentation explains that the active layer is drawn on top, selected objects are also drawn on top, and non-active layers may be dimmed or hidden. Those details matter because a screenshot’s draw order can differ from the underlying electrical truth. citeturn13view2turn25view0turn25view1turn25view2turn25view3turn26view0turn21view2turn21view3turn23view2turn31view0turn31view1turn31view2turn33view1turn33view2turn33view3

KiCad’s schematic side exposes similar contextual signals. The Schematic Editor includes a hierarchy navigator, properties manager, selection filter, message panel, status bar, editing canvas, and ERC. Selecting an object surfaces information in the message panel, and net highlighting can cross-probe between schematic and PCB. ERC checks for unconnected pins, illegal connections, hierarchical mismatches, and net naming conflicts, and even supports user-definable ERC warnings and errors. In a KiSurf packet, those structured facts should be transmitted as text, not left for the model to infer from pixels. citeturn16view0turn16view1turn17view0turn17view1turn17view2turn17view3turn33view3

Recent GUI-agent research gives strong guidance on what visual packets should and should not do. ScreenAI showed that explicit screen annotations describing UI elements and their locations are useful training signals for downstream LLM reasoning. Set-of-Mark prompting showed that sparse, speakable marks can materially improve visual grounding. Ferret-UI showed that UI screens contain small objects that benefit from “any resolution” handling and split high-detail views. Aria-UI argued that long textual trees can introduce inefficiency, hallucination, and bias, while action history and text-image interleaving improve grounding in dynamic task settings. ScreenSpot-Pro found that existing professional GUI grounding is weak in high-resolution environments, with the best model reaching only 18.9%, and that reducing the search area improves accuracy. RegionFocus likewise improves grounding by zooming into relevant regions and using landmarks to track attention history. citeturn5view0turn5view1turn5view2turn29view0turn34view2turn28view0turn30view0turn30view1turn34view0turn34view3turn5view4

The constraints are real. OSWorld showed that state-of-the-art GUI agents still struggle badly in real computer environments, with the best model far below human performance and especially weak on GUI grounding and operational knowledge. Another ACL study found that multimodal GUI agents are vulnerable to environmental distractions. Multimodal hallucination remains a broad reliability problem, and long-context work shows that relevant information placed in the middle of a large prompt is used less reliably than information near the edges. For KiSurf, that means packet engineering must be treated as a core architecture decision, not prompt polish. citeturn10view0turn10view1turn9view2turn27search0turn10view2turn10view3

## Canonical packet schema

The best packet for KiSurf is a **base schema with fixed ordering**. Put **policy, role, and immediate objective first**, because those are load-bearing constraints; put the **response contract last**, because models also attend well to the end of context windows; keep the mutable scene state in the middle. That ordering is an inference from long-context evidence and works well with the planning-versus-grounding split described in GUI-agent work. citeturn10view2turn10view3turn28view2turn28view3

A recommended shared base schema is:

```json
{
  "packet_version": "kisurf.v1",
  "meta": {
    "session_id": "...",
    "episode_id": "...",
    "packet_type": "chat|next_action|review",
    "surface": "pcb|schematic|split",
    "document_revision": "...",
    "preview_revision": "...",
    "timestamp": "...",
    "staleness_policy": {
      "invalidate_after_editor_mutation": true,
      "max_actions_since_frame": 0
    }
  },

  "system_policy": {
    "editor_rules": [
      "Prefer structured state over screenshot inference when they disagree",
      "Never act on hidden-layer assumptions without explicit layer state",
      "Do not invent objects, nets, violations, or tool results",
      "Use only listed tools and listed object/anchor identifiers",
      "Publish preview only if validation policy allows it"
    ],
    "safety_and_quality": {
      "require_traceable_grounding": true,
      "require_atomic_tool_calls": true,
      "allow_clarification_if_underdetermined": true
    }
  },

  "agent_role": {
    "name": "...",
    "planning_horizon": "broad|single_step|review",
    "priority": "explain|decide|execute|judge"
  },

  "task_objective": {
    "user_request": "...",
    "current_subgoal": "...",
    "success_criteria": ["..."],
    "stop_conditions": ["..."],
    "open_questions": ["..."]
  },

  "editor_state_capsule": {
    "mode": {
      "active_tool": "...",
      "tool_phase": "...",
      "surface_mode": "...",
      "dirty": true,
      "interactive_operation_in_progress": false
    },
    "canvas": {
      "viewport_world_bbox": "...",
      "zoom": "...",
      "cursor_world": "...",
      "cursor_pixel": "...",
      "grid": "...",
      "snap": "...",
      "units": "mm|mil",
      "flipped_view": false
    },
    "layers": {
      "active_layer": "...",
      "visible_layers": ["..."],
      "dimmed_or_hidden_policy": "...",
      "highlighted_nets": ["..."]
    }
  },

  "selected_objects": [
    {
      "object_id": "...",
      "type": "...",
      "refdes": "...",
      "net_id": "...",
      "net_name": "...",
      "layer": "...",
      "world_bbox": "...",
      "handles": ["origin", "pad-center", "track-start", "track-end"],
      "locked": false
    }
  ],

  "visual_context": [
    {
      "frame_id": "...",
      "kind": "viewport_raw|viewport_annotated|roi_raw|roi_annotated|preview_after|issue_crop",
      "surface": "pcb|schematic",
      "image_ref": "...",
      "world_bbox": "...",
      "resolution_px": [0, 0],
      "transform": {
        "pixel_to_world": "...",
        "world_to_pixel": "..."
      },
      "annotation_mode": "none|sparse_anchors|issue_overlay",
      "objects_shown": ["..."],
      "provenance": {
        "source_revision": "...",
        "generated_from_preview": false,
        "generated_after_actions": 0
      }
    }
  ],

  "recent_activity": {
    "recent_user_turns_summary": "...",
    "recent_actions_summary": "...",
    "recent_tool_results_summary": "...",
    "last_successful_similar_action": "..."
  },

  "retrieved_memory": [
    {
      "memory_id": "...",
      "kind": "project_convention|prior_decision|style_rule|user_preference",
      "summary": "...",
      "relevance": 0.0
    }
  ],

  "validation_facts": {
    "pcb_drc": {
      "errors": [],
      "warnings": [],
      "exclusions": []
    },
    "schematic_erc": {
      "errors": [],
      "warnings": []
    },
    "parity_facts": []
  },

  "tool_catalog": [
    {
      "tool_name": "...",
      "purpose": "...",
      "arguments_schema": "...",
      "allowed_when": "...",
      "undoable": true
    }
  ],

  "previous_attempt_summary": {
    "attempt_id": "...",
    "what_was_tried": "...",
    "what_changed": ["..."],
    "what_failed": ["..."],
    "why_it_failed": ["..."]
  },

  "omitted_context_trace": [
    {
      "omitted": "older_dialogue|full_tool_log|global_image",
      "reason": "budget|stale|irrelevant",
      "retrieval_hint": "memory_id|tool_result_id"
    }
  ],

  "response_schema": {
    "decision": "answer|ask|act|retry|rollback|publish_preview|abandon|noop",
    "message_to_user": "...",
    "rationale_brief": "...",
    "references": {
      "object_ids": [],
      "anchor_ids": [],
      "frame_ids": [],
      "validation_ids": []
    },
    "tool_calls": [],
    "confidence": 0.0
  }
}
```

Two details are especially important. First, the **live editor state capsule** should be compact and typed, not prose-heavy. KiCad can already provide exact selection, visible layers, active layer, positions, IDs, item bounding boxes, connected items, and even current selection serialized in board format, so the model should not need narrative restatements of that data. Second, the schema should always include an **omitted-context trace**, because that lets the system stay budget-aware without silently dropping information that the model might later need. The recommendation to summarize rather than replay full logs is supported by both long-context evidence and GUI-agent work that explicitly benefits from summarized action history. citeturn25view3turn26view0turn21view3turn28view0turn9view0turn10view2turn10view3

## Visual context and grounding

The right default for KiSurf is **one global frame plus one local frame**. Send **exactly one full viewport image** when the model needs scene context, and pair it with **one tighter ROI crop** when the action is local. That recommendation follows from evidence that professional high-resolution GUIs are difficult, that smaller search areas improve grounding, and that UI agents benefit from multi-scale views when targets are small. citeturn34view3turn34view2turn34view0turn5view4

The practical visual policy should be:

- **Global frame**: current full viewport or current render, long side around **1280–1600 px** after preserving aspect ratio.
- **Local frame**: ROI around the selection, candidate placement/routing region, or active issue, long side around **1024–1536 px**, with roughly **10–20% margin** around the tight world bounding box.
- **Optional review frame**: before/after preview in the same framing.
- **Optional issue frame**: a validation crop centered on the highest-severity DRC/ERC marker.

Those exact numbers are design recommendations, but they are aligned with the literature: GUI work increasingly needs high-resolution handling; Ferret-UI explicitly split screens into multiple sub-images to magnify small elements; Aria-UI extended to much larger effective resolutions while preserving aspect ratio; and ScreenSpot-Pro found professional high-resolution grounding weak unless the search area is reduced. citeturn34view2turn34view0turn34view1turn34view3

For annotations, KiSurf should usually send **both a raw render and a sparse annotated companion**, but not for every frame. Set-of-Mark style overlays help grounding, yet Aria-UI also notes that too many tags can hurt performance. The best compromise is: keep the **raw image** visually clean, and put only **short speakable marks** such as `A1`, `A2`, `E1`, `V1` in the annotated version. The verbose mapping from marks to object IDs, nets, layers, box coordinates, and world coordinates should live in **text sidecars**, not be painted densely onto the bitmap. citeturn29view0turn30view0turn30view1

That means the preferred order for images in a Next Action packet is usually:

1. **Viewport raw** for global layout understanding.  
2. **ROI raw** for local geometry.  
3. **ROI annotated** for executable references.  
4. **Validation crop or preview-after** only if the task is a review or a risky edit.  

For Chat Agent packets, the order can be broader: **viewport raw first**, then **counterpart surface** if needed, then **ROI** if the user’s question is local. For review packets, the order should be **before**, **after**, then **issue crop**, because temporal comparison matters more than global scene interpretation. That structure is supported by Aria-UI’s use of context-aware action history and by review-oriented GUI-agent methods that benefit from explicit episode history rather than isolated screens. citeturn28view0turn28view1turn9view0

Grounding should not depend on pixels alone. Use **image pixel coordinates only as evidence**, not as the final executable target. The canonical executable reference in KiSurf should be a tuple of **surface + object ID + handle or world coordinate + layer/net context**. KiCad’s APIs already expose item IDs, bounding boxes, net codes/names, positions, starts/ends of tracks, via positions, footprint positions, and hit testing. So the model should point to `A3` or `object_id=... handle=track.start`, and the runtime should compile that into atomic tool calls. citeturn26view0turn23view2turn21view2turn22view2turn24view0turn24view1

A good KiSurf anchor record looks like this:

```json
{
  "anchor_id": "A3",
  "frame_id": "roi_annotated",
  "surface": "pcb",
  "object_id": "uuid:...",
  "handle": "track.start",
  "layer": "F.Cu",
  "net_id": 14,
  "net_name": "USB_D+",
  "pixel_xy": [415, 287],
  "pixel_bbox": [386, 255, 444, 318],
  "world_xy_nm": [12340000, 5678000],
  "world_bbox_nm": [12290000, 5671000, 12390000, 5685000]
}
```

For **free-space edits** such as “place this footprint here” or “route through this gap,” the model may still propose a pixel point, but only if the packet provides the frame transform and only if the runtime converts that point into world coordinates, snaps it under the active grid and layer policy, and verifies it with hit testing and rule checks before commit. This is the safest way to bridge visual observations into EDA tool calls. citeturn34view0turn12view0turn25view1turn26view0

The text-versus-image split should also be strict. **Text-only** should carry design rules, active tool mode, cursor and viewport coordinates, visible and active layers, exact object IDs, exact net names, constraint values, validation severities, and tool schemas. **Image-only** should carry geometry that is genuinely hard to serialize compactly: crowding, shape adjacency, route aesthetics, label occlusion, and visual plausibility of a preview. **Duplicate both in text and image** for the selected objects, route endpoints, issue markers, and any anchors that the model may mention in its response. That division is a direct consequence of GUI research showing that purely textual trees miss visual cues, while purely visual grounding without structured contextual state is brittle in dynamic tasks. citeturn30view2turn30view3turn28view0

## Agent-specific variants

The base schema should stay stable, but each KiSurf workflow should have a stricter packet policy.

**Chat Agent task execution** should carry broader objective context, user instruction history, project memory, and, when necessary, both PCB and schematic surface state. The image budget should usually be **one to three frames**: a full current viewport, an optional counterpart surface frame if cross-probing matters, and an optional ROI for the object under discussion. Because Chat Agent is not choosing a single atomic edit every turn, it can tolerate broader task context, but old dialogue should still be summarized rather than replayed in raw form. KiCad’s cross-probing and net-highlighting capabilities make cross-surface context especially valuable when the user is asking why a board state exists or whether a schematic/PCB mismatch is intentional. citeturn33view3turn16view1turn17view3turn10view2turn10view3

**Next Action placement** should be the narrowest decisive packet. It should include the current placement candidate, selected or intended footprint(s), relevant keepouts and nearby courtyards, active side and layer, current grid and snap policy, local DRC facts, and a very short episode history of what has already been tried. The preferred visual packet is **two or three frames**: global viewport raw, local ROI raw, and local ROI annotated. Placement is exactly the kind of task where the model needs both the global composition and a local crowding judgment. Search-area reduction and sparse landmarks matter here. citeturn34view3turn29view0turn5view4

**Next Action routing** should be even more geometry-heavy. It should include explicit route endpoints, net ID and net name, net class, width and clearance constraints, active routing layer, via policy, visible-layer state, obstacle summary, and any existing partial route. The preferred visual packet is **two to four frames**: ROI raw, ROI annotated with endpoint and obstacle anchors, optional viewport raw for broader topology, and optional validation crop if the previous route attempt failed. Routing should never rely on screenshot-only judgments because active-layer draw order, selected-object prominence, and hidden or dimmed layers can visually mislead the model. citeturn33view1turn33view2turn32view2turn26view0

**Next Action auto-fill or refill** should be mostly structured-state driven. KiCad already provides refill operations, zone objects, and DRC tooling, so the VLM often does **not need any image at all** if the question is simply whether to refill, whether a refill finished, or what zones were affected. Use images only when the refill result is visually suspicious, when a copper island or thermal pattern needs judgment, or when the system is deciding whether to present a preview to the user. The packet should focus on changed zones, relevant layers, validation deltas, and whether the refill was global or scoped. citeturn35view3turn26view0turn32view2

**Hidden attempt review** should be a separate reviewer packet, not a continuation of the actor’s prompt. It should include a one-paragraph attempt summary, the exact tool calls or edit classes used, a before image, an after-preview image rendered from the same world bbox, any high-severity validation issue crop, and a compact diff of validation facts. The response contract should allow only reviewer decisions such as `retry`, `rollback`, `publish_preview`, or `abandon`. This maps well to KiCad’s commit model, where edits can be grouped and then pushed or dropped, and to GUI-agent work showing that explicit short-term history and reflection improve multi-step decision quality. citeturn35view0turn35view1turn35view2turn9view0

**Accept-grade validation review** should be even stricter and more policy-bound. It should include the final preview provenance, DRC and ERC counts by severity, unresolved violations, exclusions, parity mismatches between PCB and schematic if enabled, and one or more validation-centered images. The model should not be asked to “be helpful”; it should be asked to **grade release readiness** against clearly stated accept criteria such as “no new unapproved errors,” “no unresolved net-name conflicts,” and “preview revision matches current document revision.” KiCad’s DRC and ERC already provide the necessary severity framing, issue localization, and object references to support that decision. citeturn32view2turn32view3turn17view0turn17view1turn17view3

## Budgeting, review loops, and failure modes

The single most important budgeting rule is to prefer **state compression over context accumulation**. Old dialogue should become a short intent summary. Old tool results should become a de-duplicated activity summary. Only the last locally relevant attempt should be carried verbatim. The packet should also record what was omitted and how to retrieve it. That design is supported by long-context evidence and by GUI-agent work showing that raw long textual contexts and poorly filtered auxiliary signals introduce inefficiency, hallucination, and bias. citeturn10view2turn10view3turn30view1

A good compression policy for KiSurf is straightforward. Send the **full viewport** only when the action depends on global layout or when the local ROI would be ambiguous without it. Send the **ROI** whenever targets are small or crowded. Omit images entirely when the action is deterministic from structured state, which will often be true for narrow refill or selection-management steps. Keep **one authoritative structured fact per concept**: do not repeat the same DRC output in full prose, full table form, and raw tool logs. Summarize previous attempts into “what changed,” “what failed,” and “what is different now.” That pattern matches the action-summarization emphasis found in GUI-agent reasoning work. citeturn34view3turn9view0

The recommended hidden-loop for KiSurf is:

1. **Actor call** with a narrow execution packet.  
2. **Do the edit in a commit transaction** or equivalent preview state.  
3. **Render** a comparable preview image or ROI-aligned export.  
4. **Run validation** such as DRC, ERC, parity, or targeted rule inspection.  
5. **Reviewer call** with before/after/issue packet.  
6. Decide to **retry, rollback, publish preview, or abandon**.  

This loop is especially natural for KiCad because the board API supports `begin_commit`, `push_commit`, `drop_commit`, `refill_zones`, and export functions for visual artifacts. citeturn35view0turn35view1turn35view2turn35view3turn31view0turn31view1turn31view2

The common failure modes in a GUI-heavy EDA system are predictable. **Small geometry misread** happens because professional high-resolution interfaces contain tiny targets; mitigate it with ROI crops and multiscale views. **Coordinate mismatch** happens when image pixels are treated as action targets rather than evidence; mitigate it with transforms, anchors, and world-coordinate compilation. **Hidden-layer ambiguity** happens because active and selected items are visually promoted; mitigate it by always sending active/visible layer state in text. **Hallucinated objects or stale previews** happen when the model reasons from old or weakly grounded frames; mitigate with provenance fields, revision IDs, and frame invalidation after edits. **Environmental distraction** happens when irrelevant UI content receives too much salience; mitigate by cropping to the working region and minimizing decorative overlays. **Context overload** happens when packets carry every prior step and tool dump; mitigate by summarizing and keeping critical constraints at the prompt edges. citeturn34view3turn34view2turn24view0turn24view1turn26view0turn33view1turn33view2turn9view2turn27search0turn10view2turn10view3

The concrete KiSurf recommendation, therefore, is a packet system that is **state-first, image-assisted, anchor-grounded, and review-separated**. Chat Agent packets should be wider and more semantic. Next Action packets should be local, typed, and short-lived. Review packets should be comparative and policy-bound. If KiSurf adopts that discipline, the VLM will reason over the editor like a native subsystem rather than like a screenshot chatbot. citeturn28view2turn28view3turn28view0turn9view0turn26view0
