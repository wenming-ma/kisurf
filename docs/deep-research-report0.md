# AI Native PCB 编辑器中 Agent 脚本执行层的最佳架构

## 核心结论

对 KiCad 改造成 AI Native PCB 编辑器这件事，**最佳方案不是“让模型直接执行 Python/Lua/JavaScript”，也不是“给每种组合动作单独做一个 tool”**。更合理的架构是一个**混合式、分层的执行模型**：

**Agent 负责规划与生成受限脚本或 IR；确定性的执行器负责把 IR 编译并解释为稳定的原子操作；所有变更先进入 Preview 事务；用户 Accept 后，再一次性提交为 KiCad 原生 undo step；可选的 WebAssembly 沙箱只承担“纯计算插件”角色，而不是主脚本层。** 这个结论与 KiCad 当前的 IPC API 方向、事务/提交机制、头less API server 能力，以及现代 agent tool-calling 系统对“少而强、强约束 schema 接口”的实践是一致的。citeturn19search8turn19search4turn23view3turn23view4turn0search15turn25search2turn10view0turn10view9turn30view0

如果只用固定组合工具，你会遇到组合爆炸；如果让模型直接在宿主中跑通用脚本语言，你会遇到安全、可审计、可预览、可回放、可版本化的问题。相比之下，**“原子操作 vocabulary + 受限脚本 IR + Preview-first transaction”**同时满足表达力、稳定性、可验证性和产品可控性。KiCad 官方已经明确把 **SWIG Python bindings** 定位为不稳定且正走向移除，而把 **IPC API** 定位为稳定、语言无关、面向现代插件开发的接口；这恰好说明你们的 AI 执行层应当建立在“稳定协议面”之上，而不是历史上紧耦合内部对象模型的脚本入口之上。citeturn17view0turn19search8turn19search4

## 设计目标与 KiCad 现实约束

KiCad 当前已经给了你们几个很关键的“现实边界”。第一，**旧的 `pcbnew` Python API 直接反映内部对象结构，而且官方明确说它会随内部重构而变化，并不稳定**；第二，**IPC API 被设计成稳定接口，并且语言无关**；第三，**KiCad 的 IPC 服务器是 request-reply 模式，消息最终仍然在 UI 线程处理**。这意味着你们不应该把 Agent 执行层设计成“任意驱动 KiCad 菜单动作的自动化器”，而应设计成**一个对 KiCad 发送稳定 CRUD/查询请求的确定性执行器**。citeturn17view0turn19search4turn33view0

KiCad 现有 API 已经足够支持“原子操作层”的基本轮廓。`Board` 对象支持 `create_items`、`update_items`、`remove_items_by_id`，也支持按类型和 UUID 读取对象、读取走线/过孔/覆铜区、读取 pad 多边形、读取连通铜对象、读取板子序列化字符串，以及 `begin_commit` / `push_commit` / `drop_commit` 这样的事务接口。`begin_commit` 打开后，变更在 `push_commit` 前不会反映到编辑器；`push_commit` 会形成**单个** undo step；`drop_commit` 可以直接丢弃未提交变更。对你们要做的 Preview/Accept 流程来说，这几乎就是一个现成的事务骨架。citeturn23view3turn10view4turn23view4turn27view0turn27view4turn24view1turn31view0turn32view0

另外，KiCad 还支持**headless `kicad-cli api-server`**，而且 `kipy.KiCad` 可以在 headless 模式下连接到该 server，并加载 board/project 文件。这一点非常重要，因为它意味着 Preview 并不一定非要污染当前 UI 会话；你们完全可以在**影子板子**或独立 headless 会话中做编译、预执行、渲染与 DRC，再把“已验证的 bundle”提交回前台。citeturn0search15turn17view3

还有一个值得直接借用的设计原则来自 KiCad 自己的 IPC 开发文档：官方明确强调 API 设计应当**概念上合理**，而不应被内部 C++ 类层级牵着走；并且 Protobuf API 的演进应尽量只做**新增字段、弃用而不重命名/重排/复用**。这对你们定义 Agent IR 非常有指导意义：**不要把 IR 设计成 KiCad 内部对象树的镜像；要把它设计成面向任务与编辑语义的抽象模型。** citeturn33view0

## 执行架构选型

### JSON DSL 与 AST 解释器

如果只看**可控性、可验证性、可审计性、可回放性**，最强的主方案仍然是 **JSON DSL / AST interpreter**。现代 tool-calling 系统本来就强调工具输入要有**清晰 schema**；Anthropic 的工具定义直接以 **JSON Schema** 为核心，MCP 也把工具暴露为带 schema 的能力面，VS Code 的 LLM tool API 也强调模型只是**生成参数**，并不真正执行工具。把 Agent 的“可执行意图”收敛到一个强类型 AST，本质上就是把这些实践推到 PCB 编辑域里。citeturn10view1turn10view2turn10view0

JSON AST 的最大优势是：你可以在真正 touching board 之前做**schema 校验、类型检查、作用域检查、引用解析、循环上界检查、操作数预算检查、几何预检、规则预检**。同时，AST 天生适合转换成**operation bundle**，再写入审计日志，后续做 Replay、Diff、Regression Test 也都更容易。结合 KiCad/Protobuf 官方对稳定 API 演进的建议，一个很好的工程做法是：**模型面向 JSON AST，执行器内部序列化面向 protobuf message**。前者对 LLM 更友好，后者对版本化与长期兼容更稳。citeturn10view1turn33view0

它的缺点只有两个。第一，直接让模型生成深层嵌套 JSON 时，可读性和 token 效率不如代码。第二，复杂几何任务里，模型写 AST 常常不如写简短脚本自然。所以 JSON AST 非常适合作为**canonical IR**，但不一定是最好的唯一“作者界面”。这是为什么最终推荐是“**作者语法可灵活，执行语义必须归一**”。citeturn10view1turn25search2

### Lua、Python、JavaScript 沙箱

把 Agent 直接接到通用脚本语言沙箱里，初看很诱人，因为变量、循环、条件、几何计算都天然存在，模型也更擅长写“代码”而不是写大 JSON。但如果你们讨论的是**产品级 AI Native 编辑器**而不是内部实验工具，这条路最大的问题不是表达力，而是**宿主控制权**。Node 官方文档直接写明 `node:vm` **不是安全机制，不应拿来运行不受信任代码**。Lua 的手册说明 chunk 默认运行在全局环境 `_ENV` 中，标准库也默认加载；虽然可以通过 `load` 提供不同环境，但它更像名字查找与宿主注入机制，而不是完整的安全边界。Python 的 embedding 文档则明确把 Python 当成向应用中嵌入的通用扩展语言。换句话说，这些运行时都很适合“扩展能力”，但都不该被当成“AI agent 直连 live board 的最终安全边界”。citeturn10view11turn10view12turn10view13

因此，这类语言最合理的位置是：**前端语法，不是最终执行语义**。也就是说，你们可以给模型一个极小的、Python-like 或 JS-like 的 DSL，让它写：

```python
n = floor(2*pi*r/pitch)
for i in range(n):
    theta = 2*pi*i/n
    p = polar(center, r, theta)
    v = create_via(at=p, drill=0.3, diameter=0.6, net="GND")
    set_metadata(v, name=f"ring_via_{i}")
```

但这段东西**不直接运行在 KiCad 宿主里**；它只进入 parser / compiler，被编译成你们自己的 typed IR，然后由确定性执行器去跑。这样你们既保留“模型更会写代码”的优势，又不会把安全、undo、preview 和审计交给通用运行时去兜底。上面的“代码式 authoring、IR 式 execution”是这类方案能落地的关键分水岭。相关工具系统的官方实践也都在强调：模型负责生成工具参数，宿主负责执行。citeturn10view0turn25search1turn25search0

### WebAssembly 插件沙箱

**WebAssembly 不是最佳主脚本层，但它是最佳“受限扩展计算层”候选。** WebAssembly 官方强调其执行环境是**内存安全、沙箱化**的；Wasmtime 文档进一步说明，默认情况下组件**无法访问文件系统或环境变量**，文件系统访问遵循**capability-based security model**；WIT 又把接口定义限定在“contracts”，而不是一般编程语言的行为语义。对你们来说，这意味着：如果以后想支持第三方几何库、阵列算法、参数化生成器，WASM 是一个非常好的插件壳。citeturn5search3turn10view9turn30view0turn10view10

但 WASM 也不适合做 LLM 的一线 authoring 语言。原因不是安全，而是**工作流摩擦**：要编译、要封装接口、要处理 host binding、要做运行时调度。它很适合承载“纯函数式、可预算、可隔离”的几何与分析内核，例如 circle packing、spiral generation、复杂 polygon offset、阵列布局、EM 近似计算等；不适合承担与 board 会话交互频繁、需要大量上下文读取与对象引用的主控制流。更好的做法是：**IR 解释器是总控；WASM 插件只是 IR 可调用的纯计算函数库。** citeturn30view0turn10view10turn10view9

### Workflow graph 与 node graph

Workflow graph / node graph 在编辑器世界里并不罕见。Unity 官方把 Visual Scripting 定义为**visual, node-based graphs**；Unreal 既有 Blueprint visual scripting，也有面向编辑器自动化的 Editor Utility Widgets、Editor Utility Blueprints 和 Scriptable Tools System。对于**人类作者**来说，这些图式系统很适合做可视化宏、团队共享模板和半技术用户工具面板。citeturn21view2turn21view1turn4search5

但从 Agent 友好性看，node graph 更适合作为**人类 UI 表面**，而不是 LLM 的主输出格式。这里是一个带推断成分的判断：图节点、连接关系、布局状态对模型来说 token 成本高、可读性差、diff 不友好，而且很难像文本脚本那样自然表达局部修改。因此，node graph 可以保留，但最好作为**“人可以编辑、系统可视化呈现、最终仍编译到同一个 IR”** 的上层产品形态，而不是基础执行合同。这个推断与 Unity/Unreal 把 visual scripting 定位在原型、协作、工具化层的做法是一致的。citeturn21view2turn21view1

### 纯 LLM tool-calling 加 operation bundle

纯 tool-calling 当然可用，而且现代 agent 框架都这样工作。VS Code 的文档明确说模型只是根据上下文生成工具参数；GitHub Copilot 的 agent loop 文档也说明：CLI 是机械执行者，模型决定何时继续调用工具、何时停止，一个任务往往会产生多个 turn。citeturn10view0turn22view0

问题在于，**如果把 PCB 编辑直接做成“一次 tool call 一个原子动作”**，那你们会遭遇三件事：上下文往返过多、Preview 难做成一个整体、以及中间状态不稳定。环形覆铜 + 一圈过孔这种任务，本质上就是一个带几何计算、循环、引用和批处理的“局部程序”；把它拆成几十个轮次的 tool-calling，不如让模型一次性提交一个“程序”，由宿主在**同一个事务上下文**里解释。也就是说，tool-calling 仍然有用，但它更应该承载 **`propose_ir_preview` / `inspect_preview` / `accept_preview`** 一类高层交互，而不是承载每一次 `create_via`。这也是为什么最终建议是**tool-calling 做外层协议，IR 执行器做内层执行。** citeturn10view0turn22view0turn25search2

## 对 Agent 最友好的接口形态

最适合 Agent 的并不是单一格式，而是**三层表示**：

**自然语言计划层**：让模型先表述任务意图、约束和成功标准。  
**规范化 IR 层**：系统把计划编译成强类型 AST。  
**原子操作 bundle 层**：执行器把 AST lowering 成 create/update/remove/query 的原子序列。  

这种分层和 GitHub Copilot 的 agent loop、VS Code 的 tool API 很像：模型负责决策与参数生成，宿主负责机械执行、状态管理和结果反馈。citeturn22view0turn10view0

这里有一个很重要、也容易被忽视的结论：**“底层只有稳定原子操作”不等于“要把每个原子操作都作为单独 tool 暴露给 LLM”。** Anthropic 的官方工具设计建议恰恰强调：**合并相关操作，减少工具数量，可以降低 selection ambiguity**；同时要给出非常详细的描述和 schema/example。对 PCB 编辑器最自然的做法是：**对模型暴露少数几个高层工具，对执行器暴露完整原子 vocabulary。** 例如：

- `read_board_context`
- `propose_ir_preview`
- `inspect_preview`
- `accept_preview`
- `reject_preview`
- `undo_last_agent_commit`

而在 `propose_ir_preview` 的参数里，携带完整的 IR 程序。这样做既符合 LLM 的工具调用习惯，也保留了你们想要的“组合能力无限、底层原子稳定”。citeturn25search2turn10view1turn10view2turn10view0

就“模型到底该写代码、写 JSON AST、还是写 declarative plan”这个问题，我的建议是：

**内部 canonical form 必须是 typed IR。**  
**模型外部 authoring form 可以是“受限代码”或“半声明式脚本”。**

原因很现实：当前模型通常更擅长写带变量、循环、几何公式的短代码，而不是手工拼很深的 JSON。你们完全可以给它一个极小的脚本表面语法，但这层语法只能编译到 IR，不能直接拿到 live board mutation 权限。IR 本身则应当是强类型、可 schema 校验、可版本化、可做静态分析的结构化表示。Anthropic 的工具文档和 MCP 的工具 schema 设计，都在支持这种“**模型写结构化参数，宿主做真实执行**”的方向。citeturn10view1turn10view2turn25search0

我建议 IR 至少包含这些能力：**变量绑定、范围循环、条件分支、纯几何函数、对象句柄引用、批量 map/filter、错误策略、事务边界、预览元数据**。句柄不要直接依赖运行前的对象 UUID；对“新创建对象”应使用临时符号，如 `via[i]`、`zone.main`，由执行器在运行后回填真实 KIID。KiCad 已经支持按 KIID 读取对象、按 UUID 更新对象，也支持把整板或选择区导出为板文件字符串，因此你们完全可以在运行时维护“**symbolic handle -> real KIID**” 的映射，并在 Accept 前后做一致性检查。citeturn31view0turn32view0turn24view1turn31view2

为什么不应该给每种组合动作单独定义接口？因为那会同时违反两条成熟实践。第一，Anthropic 官方明确建议**不要为每个细碎动作分别做工具**，而是把相关动作收敛到更少、更有表达力的工具中。第二，KiCad IPC 开发文档明确建议 API 设计要**概念上合理、便于未来演进**，而不是跟着内部实现和眼前用例不断长出脆弱的新 message/field。对你们这个场景，**组合动作不是 API surface，而是 script/IR surface。** API surface 应该是稳定原子与少数会话级工具；脚本层才是组合爆炸发生的地方。citeturn25search2turn33view0

## 安全、Preview 与 Accept 流程

你们的安全模型最好从一开始就采用**能力缺省为零**，而不是“有能力再逐项屏蔽”。如果主执行层是你们自己的 IR 解释器，那么文件/网络访问是最容易彻底禁掉的，因为 IR 内根本就不存在这些 opcode。若未来引入 WASM 计算插件，Wasmtime 默认就拒绝系统资源访问，文件系统也遵循 capability model；这非常适合作为“几何/分析插件”的安全边界。相反，如果你们采用 JS/Python/Lua 直接执行业务脚本，就必须自己再额外处理 import、标准库、host binding、CPU time、内存与死循环问题，而这些通用运行时本身并不天然等于安全沙箱。citeturn10view9turn30view0turn10view11turn10view12turn10view13

因此，安全栈应当分为至少五层。第一层是 **schema/type validation**；第二层是 **semantic validation**，比如 layer/net 是否存在、钻孔与直径是否在允许范围内；第三层是 **geometry preflight**，检查自交、越界、环宽为负、pitch 异常等；第四层是 **design-rule preflight**，包括 zone refill 后的快速检查与可选 DRC；第五层才是 **user approval**。KiCad 文档明确指出，运行 DRC 前要确保 zone 已经 refill，而 `kicad-cli pcb drc` 可以对 board 文件执行设计规则检查；这正适合放在 Preview pipeline 里。citeturn18search1turn18search0turn18search18

我建议 Preview/Accept 流程设计成下面这样：

```text
Agent plan
  -> compile to typed IR
  -> static/semantic/geometry validation
  -> execute on shadow board or unopened commit
  -> refill zones
  -> run fast checks + optional full DRC
  -> build semantic diff + visual preview
  -> user Accept / Reject
  -> Accept: replay or push as one KiCad commit
  -> Reject: drop transaction
```

这个流程与 KiCad 自身的 commit 接口高度契合：`begin_commit` 之后的变化在 `push_commit` 前不会反映到编辑器；`push_commit` 形成一个 undo step；`drop_commit` 可以丢弃整个预览事务。结合 headless API server，你们甚至可以把 Preview 放到独立会话里生成 SVG/PDF/3D render、DRC 报告和 diff 摘要，再把最终 bundle 提交到前台。KiCad API 已支持导出 SVG、PDF、render，也支持把整板序列化成 board file string，这些都可直接成为 preview artifact。citeturn23view3turn10view4turn23view4turn24view0turn24view1turn0search15turn17view3

关于“Preview 是否应该显示每个原子操作”，我的建议是：**默认不应该。** 默认视图应该是**按语义分组的 diff**，例如“新增 1 个环形覆铜区”“沿圆周新增 24 个过孔”“为 24 个对象设置 net 与 metadata”，并配上受影响对象数量、网络、层与边界框摘要。用户如果展开细节，再看到每个 atomic op 的列表和原始 IR。这个建议并不是说原子操作不重要，而是为了产品可读性——成熟编辑器普遍把大量细节变化合并成一个事务或 undo group。Unity 文档明确说明 undo 会按 group 组合，AutoCAD 支持 nested transaction，Fusion 的 `executePreview` / `isValidResult` 也体现了“preview 计算可复用到最终确认”的思想。citeturn21view3turn10view7turn14search1

部分失败处理上，我建议默认采取**preview 阶段 fail-closed，accept 阶段 atomic commit**。也就是说，在 Preview 期间，独立批次内可以标记软失败，比如某些 metadata 被裁剪、某些参数被 clamp；KiCad 的 `update_items` 文档就说明返回值可能不同于输入，因为属性可能越界后被 clamp，或更新未完全应用。对于这种情况，Preview 层应该把“修正后实际值”和“失败原因”明确展示。只要出现阻断性错误，例如 zone 自交、对象越过设计边界、规则冲突严重、board 在 Accept 前已被别人改动，就直接不允许 Accept。citeturn32view0turn24view1

Replay 和 stale preview 检查也应该内建。KiCad 可以返回整板的 board file string，因此最简单的做法是在 Preview 生成时计算一个 **board revision hash**；Accept 时如果当前 hash 已变化，就拒绝直接提交，要求重新编译或重放。这样可以避免“预览是在旧板子上算的，但用户 Accept 时板子已改了”的竞态。由于 KiCad IPC 当前是 request-reply，且最终处理在 UI 线程上，不能指望通过异步通知来自动修正这些竞态；执行器必须自己做 journal、hash 和 precondition 管理。citeturn24view1turn33view0

在交互安全方面，还可以直接借鉴 VS Code agent mode 的做法：工具调用被视为高风险能力，需要 approval，而且 approval 可以按会话/工作区/应用粒度记忆。你们的 Accept 本质上就应该是 PCB 域中的“高权限工具批准点”。Preview 是探索；Accept 才是写权限。citeturn28view0

## 可借鉴的工程实践

如果把 KiCad、Blender、FreeCAD、AutoCAD、Fusion、Unity、Unreal 和 VS Code/Copilot 放在一起看，会看到一个非常一致的模式：**真正成熟的编辑器自动化，不靠“任意脚本直接改 live state”作为终局，而靠事务、预览、分组 undo、编辑器内专用工具面和受控执行边界。** Blender 的官方 Python API 明确要求修改 Blender 数据的 operator 应开启 `UNDO`；FreeCAD 官方特性说明也强调其 undo/redo 是 transaction-oriented，undo stack 存的是 document transactions 而不是单个动作；AutoCAD .NET 支持 nested transactions；Unity 的 Undo API 会把多个变化组合在一个 group 中；Unreal 编辑器自动化推荐使用 Editor Utility Widgets、Editor Utility Blueprints 与 Scriptable Tools System。citeturn11search0turn2search1turn10view7turn21view3turn21view1

Fusion 360 则给了 Preview/Accept 设计一个很直接的参照：`executePreview` 阶段生成的结果，如果 `CommandEventArgs.isValidResult` 为真，OK 时可以复用 preview 结果。这和你们希望的“所有操作先生成 Preview，Accept 后才真正修改 PCB”高度同构。也就是说，你们并不是在发明一个奇怪的新交互模式，而是在把成熟 CAD/DCC 的 command-preview 模式带到 AI agent 执行域。citeturn14search1turn13search0

VS Code 与 Copilot 则说明了 Agent 分层的另一个关键点：**模型不是执行器，宿主才是执行器**。VS Code 文档明确说，模型只负责生成工具参数，真正的工具由扩展宿主执行；GitHub Copilot 的 agent loop 文档明确说 CLI 是 orchestrator，模型决定是否继续调用工具，整个 loop 是机械地“模型请求 -> 宿主执行 -> 结果回给模型”。你们的 AI Native EDA 架构也应采纳同一原则：**Agent 做 planning；executor 做 execution；verifier 做 checking；Editor 做 preview and commit。** citeturn10view0turn22view0

KiCad 自己的官方 API 也已经在朝这个方向走。SWIG Python bindings 被标注为 deprecated，而 IPC API 被标注为 stable interface；旧式 `run_action` 接口甚至明确带 warning，说 action 名称不保证稳定、而且可能有意外副作用。对 AI Native 产品来说，这实质上是在告诉你们：**不要把 AI 建在菜单动作自动化之上，要建在稳定的对象 CRUD 和查询协议之上。** citeturn19search8turn19search4turn20view0

## 推荐方案与演进路线

### 短期可落地方案

短期最值得落地的方案，是一个**不引入通用脚本运行时、直接建立 typed IR 执行器**的混合架构。具体来说：

第一，**以 KiCad IPC API 为宿主边界**，不要继续投入 SWIG 绑定作为 AI 核心执行面。KiCad 官方已经把 IPC API 定位为稳定、语言无关的现代接口，把 SWIG 绑定定位为 deprecated。citeturn19search8turn19search4

第二，**把内部操作 vocabulary 固定为少数稳定原子类**：  
读上下文类，例如 get board / get items / get vias / get zones / get connected items / get pad shapes / get design rules；  
写入类，例如 create / update / remove / group / move / set props；  
事务类，例如 preview begin / accept / reject / undo。  
KiCad 当前 API 已经覆盖了大部分读取面与 CRUD 面。citeturn27view0turn27view4turn31view0turn32view0

第三，**对模型暴露少数高层工具，而不是几十上百个细原子 tool**。推荐模型主要调用：`read_board_context`、`propose_ir_preview`、`inspect_preview`、`accept_preview`、`reject_preview`。外部工具数量少，内部 IR op 数量可以多。这样既符合 Anthropic 的“fewer, more capable tools”建议，也符合 tool-calling 的 schema 化最佳实践。citeturn25search2turn10view1turn10view0

第四，**Preview-first**：用 shadow board 或 `begin_commit` 先执行，再 `refill_zones`，然后跑快速检查和可选 DRC，生成语义 diff + 视觉预览。Accept 时统一 `push_commit`，Reject 时 `drop_commit`。这是最容易在 KiCad 现有能力上搭起来的一条路。citeturn23view3turn23view4turn10view4turn18search0turn18search1

### 中期演进方案

中期我建议加入一个**“受限代码表面语法”**，但仍然编译到同一个 canonical IR。原因是模型通常更擅长写简短脚本而不是长 JSON，而你们又需要变量、循环、条件与几何函数。这个表面语法可以长得像极简 Python 或极简 TypeScript，但要非常小：没有 import、没有文件/网络、没有反射、没有宿主对象访问，只允许纯表达式和你们定义的 builtins。它的角色不是“运行时”，而是“authoring syntax”。相关 tool 文档已经证明：模型在有清晰 schema、示例和参数解释时，生成结构化输入会更稳定。citeturn10view1turn25search2

中期的另一条演进，是把**visual macro / node graph** 引入给人类用户，而不是给模型。人类可以在 UI 里录制、拖拽、编辑流程图；系统统一把这些流程图编译成相同 IR。这样你们未来会同时拥有三种入口：自然语言、简脚本、可视化图，但**只有一个执行语义和一个 preview/commit 管道**。这一点能极大降低后续维护成本。Unity/Unreal 的官方实践已经证明，visual scripting 非常适合协作、原型与工具化。citeturn21view2turn21view1

如果要开放第三方算法扩展，中期可以把**WASM 作为纯计算插件 ABI** 引入。接口用 WIT 描述，权限默认归零，只把明确授予的几何数据和参数传进去。这样既能给第三方供应商留算法生态，又不会把网络、文件和 live editor control 暴露出去。citeturn10view10turn10view9turn30view0

### 长期 AI Native 架构

长期最理想的 AI Native EDA 架构，不应只是“LLM 调 KiCad API”，而应是一个**分角色系统**：

```text
User intent / product UI
        ↓
Planner Agent
        ↓
Typed Script / IR Compiler
        ↓
Deterministic Executor
        ↓
Preview Verifier
        ↓
KiCad Atomic Operation Layer
        ↓
KiCad IPC API
```

在这个结构里，Planner 负责任务分解、参数推理、策略选择；Compiler 负责把计划转成可验证 IR；Executor 只做确定性执行；Verifier 负责几何与规则检查、视觉渲染、diff 归纳；KiCad 层只提供稳定的对象 CRUD / 查询 / 事务接口。GitHub Copilot 的 agent loop、VS Code 的工具宿主模式、KiCad 自己的稳定 IPC API 方向，都在支持这种“**决策与执行解耦**”的系统形态。citeturn22view0turn10view0turn19search4turn33view0

长期版本中，我还会建议把 Preview 做成“**可分支的编辑提案对象**”而不是简单的临时结果：它应包含 board base hash、IR、compiled bundle、semantic summary、render artifact、DRC result、user decision 和最终 commit ID。这样 Preview 不只是一个 UI 层弹窗，而是可以被审计、缓存、重放、回归测试、甚至用于模型训练反馈的第一类对象。KiCad 已经提供了生成 board string、selection string 和单事务提交的基础能力，这类提案对象完全有现实落点。citeturn24view1turn31view2turn23view3turn10view4

最终，**原子操作、组合工具、脚本层、Agent 层**最好做成下面这样的职责分工：

- **原子操作层**：稳定、少变、概念清晰的 CRUD/查询/事务原语。  
- **组合工具层**：给人类或特定工作流的 convenience abstraction，可由脚本层生成，不应成为长期稳定 API 的主战场。  
- **脚本层**：表达循环、变量、条件、几何与批处理，canonical form 是 typed IR。  
- **Agent 层**：负责计划、选择策略、填写参数、调用 Preview/Accept 生命周期。  

这就是为什么**不应该给每种组合动作单独定义接口**：稳定的是原语，不稳定的是任务组合；产品真正需要稳定演进的是**协议与 IR**，不是每个宏动作的名字。KiCad 的稳定 API 原则、Anthropic 的工具设计原则，以及现代 agent 工具宿主架构，三者都在指向同一个答案。citeturn33view0turn25search2turn10view0