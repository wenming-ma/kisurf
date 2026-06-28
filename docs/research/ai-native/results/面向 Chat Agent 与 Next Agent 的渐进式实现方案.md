# 面向 Chat Agent 与 Next Agent 的渐进式实现方案

## 核心判断

你的需求方向是对的：**先把系统做成“可运行、可观察、可迭代”的最小闭环，而不是一开始追求完备的功能矩阵**。Anthropic 总结其一线客户与自建系统经验时明确指出，最成功的 agent 往往不是从复杂框架起步，而是从**简单、可组合的模式**起步；OpenAI 也建议优先把**单 Agent + 清晰工具 + 明确指令**做好，只有当提示逻辑过于复杂、工具重叠严重或单 Agent 持续选错工具时，再考虑进一步拆分为多 Agent。就你当前的场景而言，这意味着：**先把会话存储、上下文投影、状态驱动会话切换、原子工具、脚本执行、观测追踪**做扎实，再谈摘要、Memory、复杂合成工具。 citeturn11view0turn11view1

更具体地说，你的需求可以被压缩成一句工程目标：**本地完整保存会话，全量数据永不丢；送给模型时只投影出“当前任务真正需要的最小上下文”；状态变化触发新会话；工具尽量少而精，复杂操作优先交给脚本执行层**。这与当前主流 agent 实践高度一致：会话状态与模型可见上下文应当分离，长会话需要压缩或裁剪，工具需要高信号、低歧义、低 token 成本，并且整个运行链路要可追踪、可评估。 citeturn3view1turn3view2turn3view3turn6view1turn11view2

## 最小可行架构

我建议把系统拆成四个稳定层，而不是一开始就把所有逻辑揉进 Agent 主循环里：

```text
UI
  └─ Session Manager
       ├─ Transcript Store        # 本地长 JSON，会话全量事实源
       ├─ Context Assembler       # 送给模型前的“上下文投影器”
       ├─ Action State Machine    # Wiring / Layout / Chat 等状态
       └─ Tool Runtime
            ├─ Atomic Tools
            ├─ Thin Composite Tools
            └─ Script Executor
```

这样拆的原因很直接。OpenAI 的会话状态文档将 conversation 视为长期对象，其中既可以存 message，也可以存 tool call、tool output 等项目；LangGraph 也把短期记忆视作 thread 级状态，并通过 checkpoint 或持久化机制在中断后恢复。这说明**“保存什么”**与**“每次调用模型时喂什么”**在工程上本来就应该是两个层次。对你的实现来说，即便底层只是本地 JSON 文件，也建议在概念上分成：`full transcript` 和 `model input projection`。前者是完整历史；后者是每次调用模型时临时拼装出的、可压缩的上下文窗口。 citeturn3view1turn3view5turn7view2

因此，你现在这套“一会话对应一份本地长 JSON”的方案**完全可以作为第一阶段实现**，但要加一个非常关键的工程约束：**压缩只能发生在“送模阶段”，不要直接改写或销毁原始会话事实**。原因有两个。第一，后续你要做摘要、消息精简、Memory 提取，本质都依赖原始 JSON 作为事实来源；第二，压缩策略本身会持续演进，若一开始就在存储层删除中间消息，后面很难回放、审计或重建更好的摘要。这个思路与长程 Agent 的“短期上下文可裁剪，长期状态需可恢复”原则一致。 citeturn3view3turn4view0turn5view3

我建议你把单个 session JSON 控制在下面这类结构上，保持简单但为后续优化留钩子：

```json
{
  "session_id": "sess_xxx",
  "parent_session_id": "sess_prev_or_null",
  "session_type": "chat|layout|wiring|other",
  "project_id": "proj_xxx",
  "board_id": "board_xxx",
  "system_prompt_template_id": "chat_v1",
  "fixed_context": {
    "project_snapshot": {},
    "board_snapshot_ref": null,
    "user_preferences": {}
  },
  "action_state": {
    "name": "chat|layout|wiring",
    "phase": "idle|planning|executing|reviewing|done",
    "payload": {}
  },
  "messages": [
    {
      "id": "msg_xxx",
      "turn_id": "turn_xxx",
      "role": "user|assistant|tool",
      "kind": "text|tool_call|tool_result|summary|image",
      "content": {},
      "created_at": "2026-06-28T10:00:00+08:00",
      "meta": {
        "tool_name": null,
        "tool_args": null,
        "image": null,
        "token_estimate": null,
        "synthetic": false
      }
    }
  ],
  "compactions": [],
  "memories": [],
  "artifacts": []
}
```

这里最重要的不是字段名，而是三点：**完整消息保留、状态单独记录、工具产物可引用**。只要你做到这三点，后续无论是加摘要、做 replay、抽取 Memory、还是替换模型供应商，成本都会很低。 citeturn3view1turn13view1

## 上下文管理设计

你的第一阶段压缩方案可以直接落地，而且建议就按你写的思路执行：**保留系统提示词与固定上下文，保留最后几轮完整交互，直接丢弃中间消息**。这不是“土办法”，而是合理的起步策略。OpenAI 的会话记忆 cookbook 把一种成熟做法定义为“保留最后 N 个 user turn，裁掉更早内容”；而且它特别强调，裁剪时应保留**完整 turn 边界**，因为一个 turn 不只是用户一句话，还包括其后的 assistant 回复、推理片段、工具调用和工具结果，直到下一个 user message 为止。这样做的好处是：保住最近的任务连贯性，同时快速抑制上下文膨胀。 citeturn14view0

这类策略之所以必要，不只是因为上下文窗口有限，也是因为**更多上下文不自动等于更好效果**。Anthropic 在 context window 文档里明确指出，随着 token 增长，准确率和检索能力会出现下降，也就是所谓的 “context rot”；OpenAI 也把 compaction 视为长会话中的正式机制，用来在保留关键状态的前提下压缩上下文、平衡质量、时延与成本。换句话说，**你不是在“删信息”，而是在主动做上下文策展**。 citeturn9view0turn4view0

基于你的需求，我建议第一阶段把上下文组装流程写成一条非常清晰的确定性流水线，而不要交给模型“自由决定保留哪些消息”：

```text
组装顺序
1. System Prompt 模板
2. 固定上下文
   - 当前项目摘要
   - 当前板子/视图/选择对象
   - 当前 action_state
3. 最近 K 个完整 turn
4. 如超限，先剔除较早 turn 中的 tool_result 详细内容
5. 仍超限，再减少 K
6. 仍超限，再触发摘要消息注入
```

这里最关键的一条是：**“先裁工具结果，再裁 turn 数，再上摘要”**。原因是 Anthropic 在 context engineering 和 tool design 文档中都明确提到，长程 Agent 应优先丢弃**冗余 tool output**，保留架构决策、实现细节、未解决问题和下一步目标；同时工具返回给模型的内容应尽量高信号、低 token 成本，必要时通过分页、过滤、截断等机制控制规模。你的“后续把工具调用结果去掉，只保留调用了哪些工具”这个方向，和这些建议是高度一致的。 citeturn3view3turn6view0turn6view2turn9view0

等第一阶段跑稳后，再做你提出的三条优化路径会非常自然：

第一条是**中间历史摘要**。OpenAI 的 session memory 示例就是把旧历史总结成一对 synthetic message：一个“总结此前对话”的虚拟 user message，加一个生成出来的 assistant summary。更重要的是，它给出的总结原则非常适合你这个场景：摘要要保留时间顺序、关键里程碑、工具调用效果、当前阻塞项、下一步建议，并对不确定事实明确标记，而不是猜测。把这些原则改写成 EDA/板卡场景即可。 citeturn14view0

你这个场景里，摘要建议固定成下面这些字段，而不是自由文本：

```text
Project Goal
Current Board Scope
Key Constraints
Operations Applied
Tool Calls and Outcomes
Open Issues / Blockers
Latest Visual State
Next Recommended Action
```

这类**结构化摘要**比长段自然语言更适合继续喂给模型，因为它更接近“交接单”而不是“聊天记录”。也更方便后面直接抽成 Memory。这个设计方向与 OpenAI 对摘要 prompt 的建议，以及 Anthropic 对“保留关键决定、未解问题、实现细节”的压缩原则是一致的。 citeturn14view0turn3view3

第二条是**消息精简**。这里我建议不要简单把所有 tool_result 从原 JSON 里删掉，而是在 `Context Assembler` 中引入两种消息视图：`full` 和 `compact`。`full` 保留原始结果；`compact` 只展示：

```json
{
  "tool_name": "get_board_view",
  "args_summary": {"layers": ["Top"], "selection": "nets:VCC"},
  "result_summary": "returned image + 24 highlighted objects",
  "artifact_ref": "artifact_123"
}
```

这样你既实现了“送模时瘦身”，也保住了本地可追溯性。Anthropic 的工具文档明确建议把工具响应设计成高信号输出，并可通过 `concise` / `detailed` 之类的格式控制粒度；MCP 规范也支持工具返回 text、image、resource link 和 structured content，因此你完全可以把“详细结果”降格为 artifact 或 resource ref，只把摘要信息留在上下文里。 citeturn6view2turn13view1

第三条是**Memory 提取**。这个一定要放到后面做，因为 Memory 是“从原始 JSON 中提炼稳定事实”，而不是“替代会话上下文”。LangGraph 的 memory 文档把长期记忆分成跨会话可共享的数据，并区分 semantic、episodic、procedural 等不同类型。落到你的系统里，我建议未来只先做两种：一种是 **semantic memory**，比如用户长期偏好、项目规则、常见布局约束；另一种是 **episodic memory**，比如上次某块板子的布线失败原因、某脚本成功修复过的典型问题。先不要做自动写回提示词优化或复杂行为学习。 citeturn5view3turn1search18turn1search10

## Next Action 与会话状态设计

你提出“用户激活某个功能，比如进入布线或布局状态时，就自动新开一个会话”，这是一个非常好的边界划分。因为一旦你把状态切换与会话切换绑定，**会话就不再只是 UI 层的聊天容器，而是某个任务段落的执行单元**。这和 LangGraph、AutoGen StateFlow 一类状态驱动架构的核心思想完全一致：系统由 `State`、`Nodes`、`Edges` 组成，节点做事，边定义下一步；当状态变化时，执行路径切换，且每个 thread 或 session 都能被独立恢复。 citeturn7view1turn7view0turn7view2

因此，我建议把 Next Action 落成一个**显式状态机**，而不是“靠 prompt 里的一段自然语言约定”。最小版本就够了：

```text
Idle
  ├─> Chat
  ├─> Layout
  └─> Wiring

Layout
  ├─> Planning
  ├─> Inspecting
  ├─> Executing
  ├─> Reviewing
  └─> Done / Suspended

Wiring
  ├─> Planning
  ├─> Inspecting
  ├─> Executing
  ├─> Reviewing
  └─> Done / Suspended
```

每次用户进入 `Layout` 或 `Wiring` 时，新建一个子会话：

```json
{
  "parent_session_id": "sess_chat_main",
  "session_type": "wiring",
  "action_state": {
    "name": "wiring",
    "phase": "planning",
    "payload": {
      "board_id": "board_001",
      "selection": ["net:VCC", "component:U3"],
      "constraints_ref": "rule_pack_A"
    }
  }
}
```

这样做的收益有三层。第一，压缩策略天然被**限制在单个 action 会话内部**，不会把“板级大讨论”和“某次布线执行细节”混在一起。第二，你可以随时从父会话跳回子会话，或者从子会话提炼结果回写到父会话摘要。第三，后续如果你要支持中断恢复、审批、重跑，状态机会比自然语言约定稳定得多。 citeturn7view1turn7view2

在实现上，我建议把“状态”真正变成运行时参数，而不是藏在 message 文本里。OpenAI 的 agent 实践建议用**单一基础提示模板 + 策略变量**管理复杂度，而不是维护大量独立 prompt；LangGraph 也强调 state 是共享内存，节点应读写结构化状态，而不是反复操作格式化文本。你的 Chat Agent 和 Next Agent 可以共用一套基础 prompt，只通过 `session_type`、`action_state.name`、`action_state.phase`、`allowed_tools`、`policy_flags` 等变量来控制行为差异。 citeturn11view1turn3view4

还有一个很实用的细节：**不同状态只暴露不同工具子集**。OpenAI 的 function calling 文档支持通过 `allowed_tools` 让模型在某个请求里只看到特定工具集合；对于有副作用的调用，还可以用 `parallel_tool_calls: false` 保证一次最多执行一个工具，避免多个写操作并发导致状态混乱。落到你的系统里，就是：`chat` 状态暴露信息获取类工具；`layout` 状态暴露布局、检测、视图相关工具；`wiring` 状态暴露布线、规则检查、脚本执行工具；真正会写板子、落盘或改工程的动作全部串行执行。 citeturn5view1

## Toolbox 与脚本执行设计

这部分我完全赞同你的判断：**原子工具要完备，合成工具要克制，复杂流程优先交给脚本执行补位**。Anthropic 在工具设计文档里明确提醒，更多工具并不自动带来更好结果；真正有效的是少量、边界清晰、符合任务天然分解方式的工具。OpenAI 也建议先把单 Agent 的工具设计与指令体系做好，而不是很早分裂成复杂的多 Agent 和多层 handoff。 citeturn6view1turn11view1

所以我建议把 Toolbox 固化成三层，而不是做很多“半抽象半业务”的工具：

**原子工具层**：完备、细粒度、可组合  
**薄合成工具层**：只保留极高频、低歧义、强收益的组合操作  
**脚本执行层**：覆盖长尾流程、实验性能力、复杂批处理

这三层里，第一阶段真正必须做好的只有原子工具层和脚本执行层。薄合成工具层可以只保留极少数“人类工程师也会习惯性打包起来做”的操作，例如 `inspect_selected_region`、`apply_rule_pack_and_check` 这类，而你提到的“生成候选”之类工具，确实应当先砍掉。因为这类工具往往既不原子，也不稳定，模型还会被其模糊语义误导。Anthropic 甚至建议，当多个低层 API 总是成链调用时，应该考虑合成成一个真正以任务为中心的工具，但前提是这个工具的目标明确、上下文增益高、能减少中间输出。 citeturn6view1

在接口标准上，建议从一开始就把工具定义成**MCP/Function Calling 友好的 JSON Schema**，即便你不立刻接 MCP 服务器，也要保证未来兼容。MCP 规范已经把工具、资源、提示模板这三种能力类型拆开，并用 JSON Schema 为工具输入输出做约束；OpenAI 的 function calling 与 structured outputs 也明确推荐用 schema 严格约束参数和结果结构。这样做的好处是，Chat Agent 与 Next Agent 可以共享同一套工具注册表，而不必各写一套“给模型看的伪描述”。 citeturn13view0turn13view1turn3view6turn3view7

对于视觉信息获取，我建议你就按你的想法，只保留**一个核心视觉工具**，例如：

```json
{
  "name": "get_board_view",
  "input": {
    "board_id": "string",
    "layers": ["Top", "Bottom"],
    "viewport": {"x": 0, "y": 0, "w": 100, "h": 100},
    "selection": ["net:VCC", "component:U3"],
    "show": {
      "tracks": true,
      "vias": true,
      "pads": true,
      "components": true,
      "labels": false,
      "constraints": false
    },
    "highlight": ["DRC:error", "unrouted"],
    "response_format": "concise|detailed"
  }
}
```

这一个工具就足以覆盖你说的“过滤层级、选择显示元素、得到不同可视化状态”。关键不是工具数量，而是参数是否**简单、直观、低歧义**。Anthropic 的建议是，工具返回值应优先提供高信号字段，而不是把模型淹没在 UUID、内部技术标识、冗长原始对象里；同时可以通过 `concise` / `detailed` 控制返回粒度。MCP 规范也允许工具直接返回 image、text、resource link 等内容类型，因此视觉工具完全可以返回“缩略图 + 结构化对象摘要 + artifact 引用”。 citeturn6view2turn6view0turn13view1

脚本执行层则是你整套系统的“复杂度缓冲器”。从 OpenAI 的 Code Interpreter、Hosted Shell、Codex sandbox 到 AutoGen 的本地/容器执行器，当前主流做法都高度一致：**代码运行必须在沙箱中进行，有工作目录、资源限制、文件生命周期管理、可下载 artifact、可控网络权限，并对有风险操作设置边界**。尤其是 OpenAI 的 sandbox 文档强调，沙箱的意义不是单纯安全，而是给 agent 一个“受限但可信”的工作空间，让它能在边界内自主执行低风险操作。 citeturn12view1turn12view2turn12view3turn8search1

所以我建议你的 `run_script` 最小实现必须具备这些约束：

- 固定 `work_dir`，只允许访问项目工作区。
- 默认无外网，或只开放白名单域名。
- 必须有超时、stdout/stderr 捕获、退出码、artifact 列表。
- 脚本输入输出都要被结构化记录回 session JSON。
- 写工程、提交修改、外部同步这类高风险动作，必须走审批或确认。

这并不是“过度设计”，而是 agent 系统走出 demo 所必须具备的运行边界。MCP 规范和 LangChain/LangGraph 的 human-in-the-loop 机制都强调，工具调用应当有清晰的可视提示和用户可拒绝的授权流程；对于中断恢复类操作，LangGraph 也把 interrupt + checkpoint 作为标准做法。 citeturn13view0turn13view1turn12view0turn7view2

## 交付顺序与验收重点

如果按“尽快上真实项目”的目标来排优先级，我建议实现顺序是这样的。

**先做可工作的骨架**。包括：一会话一 JSON、状态驱动新会话、`Context Assembler` 的硬裁剪、原子工具注册与调用、单一视觉工具、脚本执行沙箱、基础 trace 记录。做到这一步，系统就已经能跑真实板级任务了，而且你能看清究竟卡在上下文、工具调用、状态切换，还是脚本执行。OpenAI 的 agent 评估文档建议，Agent 还在调行为时，应先从 trace 入手，因为 trace 能把模型调用、工具调用、guardrail、handoff 等全链路串起来，最快定位系统级问题。 citeturn11view2

**再做压缩质量优化**。当你发现“丢中间消息”已经影响完成率时，再补中间摘要、工具结果瘦身、旧视觉结果 artifact 化、状态分层工具白名单。此时你应该已经有真实 trace，可以针对失败模式去改摘要模板，而不是凭感觉设计摘要。OpenAI 和 Anthropic 都强调，生成式系统天然具有非确定性，评估和迭代必须围绕真实目标、真实数据和真实失败模式展开，而不是只看主观体验。 citeturn11view3turn10search2

**最后才做 Memory**。等你已经确认系统在真实项目上稳定后，再从本地 JSON 提取长期记忆。因为 Memory 的价值在于跨会话稳定复用，而不是给当前这个会话“补血”。LangGraph 对长期记忆的划分很清楚：它是跨 thread、跨 session 的共享状态。对你来说，真正值得抽出来的不是所有聊天细节，而是**长期规则、用户偏好、项目约束、已验证脚本、反复出现的问题模式**。 citeturn5view3

验收标准也要尽量工程化，而不是“感觉更聪明了”。我建议至少盯这几项：

- 是否能在真实项目上连续完成一个完整的布局/布线子任务。
- 上下文被压缩后，错误率是否明显上升。
- 工具误用率是否下降，尤其是状态切换后的工具选择是否更准。
- 脚本执行失败后，是否能靠现有上下文自行恢复或重试。
- 会话切到新 Action 后，是否还能快速恢复必要背景。
- 单任务 token 成本、平均时延、人工干预次数是否可接受。 citeturn11view2turn11view3

最后，再给一个非常明确的实现取舍建议：**第一阶段不要做复杂多 Agent、不要做很多模糊合成工具、不要在存储层删除原始历史、不要先上高级抽象参数、不要过早自动写入 Memory**。相反，应该把资源集中在这几个“最值钱的基础件”上：**完整 transcript、确定性的上下文投影、显式状态机、少量高质量工具、受控脚本执行、可追踪可评估的运行链路**。这与 OpenAI“先把单 Agent 能力最大化”、Anthropic“从简单可组合模式起步”的经验高度一致，也最符合你“不要一开始想着十全十美”的目标。 citeturn11view0turn11view1turn6view1

## 建议落地版本

如果把上面的方案压成一个你现在就可以开工的版本，我会建议你做成下面这个样子：

**Chat Agent**
保留一个长期主会话 JSON，负责一般对话、项目背景、任务发起、结果回顾。它不直接承载复杂执行，只负责：记录、解释、发起、总结。送模时只喂：系统提示、固定项目摘要、最近若干完整 turn，以及必要的子会话结果摘要。 citeturn3view1turn14view0

**Next Agent**
每次进入 `layout` 或 `wiring` 就创建子会话 JSON。它拿到父会话里投影出来的最小背景，加上当前 `action_state`、当前板子范围、约束/选择集，然后只暴露这一状态所需的工具子集。它的上下文压缩逻辑与 Chat Agent 相同，但压缩范围仅限该子会话内部。 citeturn7view1turn5view1

**Context Assembler**
一个独立模块，输入 session JSON，输出 model-safe messages。规则固定：先组固定上下文，再组最近 turn，再按需压工具结果，再不够才做摘要。不要让 Agent 主循环自行“决定怎么拼上下文”。 citeturn14view0turn4view0

**Toolbox**
先实现完备原子工具；只留极少的薄合成工具；视觉只保留一个 `get_board_view`；所有复杂、长尾、实验性的操作统一进 `run_script`。工具输出默认 `concise`，必要时才请求 `detailed`。 citeturn6view0turn6view1turn6view2turn13view1

**Observability**
每轮都写 trace：输入上下文长度、触发了哪些压缩、调用了哪个工具、工具结果摘要、状态是否切换、脚本是否成功、最终是否达成目标。因为没有这些数据，你很快就会陷入“感觉模型变笨了，但不知道笨在哪里”的局面。 citeturn11view2turn10search7

如果按这个版本实现，你会得到一个非常务实的系统：它不追求一开始就拥有完整 Memory、复杂编排、精细 prompt 体系，但它会有**可运行的任务闭环、清晰的状态边界、可控的上下文成本、足够强的工具基础**。对你现在的目标来说，这比追求“大而全”的体系更正确。 citeturn11view0turn11view1