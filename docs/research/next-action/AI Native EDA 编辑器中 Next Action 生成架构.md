# AI Native EDA 编辑器中 Background Preview Agent 的 Next Action 生成架构

## 核心结论

对于 KiSurf 这类深度集成到 KiCad 内核、采用 preview-first 与 typed atomic operations 的 AI Native EDA 编辑器，**Background Preview Agent 的 next-action 生成不应该是纯规则、也不应该是纯 LLM，而应当是分层的混合架构**：用**deterministic provider**处理强约束、低歧义、需要严格几何正确性的建议；用**算法型 candidate generator**生成满足设计规则的候选；再用**专用低延迟模型**做“是否该提示、提示哪一个、提示给谁看”的排序与置信度裁决；只有在语义歧义高、需要推断工程意图时，才让 LLM 参与，但它只输出**意图、区域、约束、候选排序**，而不直接输出精确坐标或最终几何。这个结论与几个相邻领域的公开工程实践是一致的：GitHub Copilot 的 next edit suggestions 已经把“下一处编辑位置预测”和“具体编辑生成”拆成两步模型；Cursor 把低延迟 Tab/next-action 预测单独做成专用模型，并明确强调“何时不该显示建议”和 accept-rate 控制；JetBrains 也把 code completion、next edit suggestions、agent mode 分层，而不是让一个大模型统一包办所有交互。对 EDA 来说，这种分层更重要，因为几何、DRC、间距、长度、差分对、过孔规则等本来就更适合由确定性求解器负责。citeturn13view1turn14view0turn13view3turn20view0turn11view1turn18view2

这也和你们在 KiSurf 仓库里已经写出的方向是对齐的：仓库 README 已明确把产品目标定义为 **“Suggest, Accept, Materialize”**，强调“让下一个合理的工程步骤以可接受的方式可视化”；同时当前分支已经具备 **Python-first AI execution session runtime、typed atomic operations、semantic shadow board、operation journal、checkpoints、rollback、native preview rendering、validation、accept replay、local protobuf Python worker/SDK** 等基础设施。换句话说，**next-action 系统最自然的输出边界，不是 raw board mutation，也不是自由文本，而是“可审计、可校验、可预览的 candidate operation bundle”**。citeturn17view1

## 现有工程实践真正说明了什么

在 IDE 侧，GitHub Copilot 的 NES 已经不是“给你一段文本补全”那么简单。GitHub 文档明确写到，NES 会**基于正在发生的编辑预测“下一处你可能要改的位置”和“该处的完成建议”**，并通过 gutter arrow 和双击 `Tab` 的方式引导用户跳到下一处修改并接受建议。更关键的是，VS Code 官方在 2026 年公开了 long-distance NES 的技术路线：当“下一处编辑”不在光标附近时，系统不再用一个通用模型硬做，而是改成**multi-model approach**，先用专门的 **location model** 预测“该跳到哪里”，再用原有 NES 模型生成该处编辑；理由也写得很直接——搜索空间从少量附近行扩展到整文件后，**错误跳转对用户信任的损害比正确建议的收益更致命，因此系统不仅要学会去哪，还要学会什么时候不要动**。这个结论几乎可以原样迁移到 EDA：对于 placement/routing/shape suggestion，系统也应该先决定“建议属于哪个对象、哪个区域、哪组网络、哪类下一步”，再让下游几何求解器生成具体预览。citeturn13view0turn13view1

Cursor 的公开材料把这件事讲得更直白。它把 Tab 视为独立的专用产品面：Cursor Tab 使用**专用低延迟模型**预测“你接下来要做什么”，既能给出当前附近的 edit，也能给出 “jumps”；其公开博客给出过 `Fusion` 模型的 p50 server latency 约 **260ms**，并强调这类系统的核心不只是模型更聪明，而是要控制**accept rate** 和“何时不建议”。Cursor 甚至公开写到：当上下文不足时，即便模型再强，也不该给建议；他们把“显示建议”本身视作一个需要优化的 policy，而非默认动作。这个工程判断对 Background Preview Agent 尤其重要，因为 CAD/EDA 中错误的 ghost preview 比代码里错误 ghost text 更扰人：它会抢视觉注意力、污染空间判断、妨碍正在进行的精确操作。citeturn12view1turn14view0

Continue 和 JetBrains 的公开产品设计进一步说明，**“next action” 不等于 “full agent”**。Continue 的 Next Edit 需要专门的 next-edit 模型，并把结果展示成 **diff overlays**，通过 `Tab` 接受、`Esc` 拒绝；JetBrains 则把 **code completion、next edit suggestions、agent mode** 明确分成不同层级，说明实际产品也倾向于把“低延迟工作流内建议”和“高自治任务执行”拆开。Cursor 首页甚至直接把这叫做 **autonomy slider**：从 Tab completion 到 targeted edits，再到 full agent。对于 KiSurf，Background Preview Agent 更接近这个光谱左侧：它应当是**低延迟、低打扰、强上下文、弱自治**的 suggestion engine，而不是一个持续跑长任务的 Chat Agent。citeturn12view3turn13view3turn20view0

EDA/CAD 侧的公开实践也指向同一个方向：**精确几何最好交给确定性算法，而不是交给语言模型直接吐坐标**。KiCad 的差分对路由器会依据设计规则中的 differential pair gap 自动生成 coupled route、fanout 和双过孔；KiCad 还支持 Attempt Finish、Attempt Finish Selected、length/skew tuning 与信号完整性 DRC。Altium 的 Interactive Routing 和 ActiveRoute 则明确是**guided interactive router**：设计师先给出选中的连接、允许层、route guide、track spacing、是否 meander、是否 pin swapping 等约束，路由器再在规则空间内求解。也就是说，在业界成熟 EDA 里，“人/智能体负责意图与约束，路由/几何引擎负责合法解构造”本来就是主流范式。把 LLM 放到这个结构里，最稳的做法不是让它输出 `(x, y, angle, width)`，而是让它定义**目标组、目标区域、路由意图、样式偏好和候选优先级**。citeturn11view0turn11view1turn18view0turn18view2turn15search6turn18view3

## 哪些 Next Action 该用 deterministic provider

如果一个建议的**目标空间很小、规则来源明确、正确性主要由现有工程规则决定**，那它就应当优先由 deterministic provider 生成。最典型的是：表格/属性面板/对话框里的参数补全，当前选择对象的 net、layer、track width、via size、zone priority、differential pair gap、bus spacing 之类的默认值继承或范围约束；以及“沿当前布线继续到最近 ratsnest anchor”“在当前差分对上下文中沿规则继续一段”“根据已选 nets 执行多线等间距继续路由”这类操作。这些动作本质上类似 IDE 里的 signature help、schema-aware completion 或 editor formatting：**候选空间是结构化的，不需要让 LLM 重新发明一个规则系统**。KiCad 和 Altium 的公开文档都显示，差分对 gap、bus spacing、via placement、route guide、width 选择等都本来就由规则和交互路由器主导。citeturn11view0turn11view1turn18view0turn18view2

更具体地说，下列类别我会建议一律以 deterministic 为主：

| 场景 | 首选生成方式 | 模型的角色 |
|---|---|---|
| 表格填充建议 | schema/rule provider | 仅在字段缺乏上下文时给默认值排序或解释 |
| panel/dialog 参数补全 | typed config provider | 只在存在多种合理工程风格时做偏好排序 |
| 单步 routing 建议 | existing router / path solver | 选择起点、终点、层偏好或“是否值得弹出建议” |
| bus routing / 多线等间距 | multi-trace router + spacing solver | 选择 bus 分组、route guide 候选、候选优先级 |
| 局部 DRC 修复 | rule-based local fix generator | 解释风险、在多个 fix 之间排序 |
| 简单 shape/via pattern | parametric geometry generator | 填补缺失参数、解释模式选择原因 |

这种分工的意义是，**LLM 不再直接控制精度和合法性，而只影响“选哪个候选、在什么时候打断用户、如何解释这个建议”**。这和 Copilot long-distance NES 的 location/edit 拆分是一致的，只不过在 EDA 里，edit 生成器不是文本模型，而是几何/规则求解器。citeturn13view1turn11view1turn18view2

## 哪些 Next Action 该用模型或模型辅助排序

当任务进入**高语义歧义**、**多个候选都合法但不知道哪个最符合当前工程意图**、或者需要综合近期行为、活动时间线、当前选择与工程阶段时，模型才真正发挥价值。典型例子包括：下一步该放 decoupling cap 还是先整理电源回流；某个器件/封装/过孔阵列/形状放置应该靠近哪个已有结构；当前工程师正在做 USB/DDR/电源入口/连接器 fanout 哪一类工作；多个候选路由走廊里哪个更符合当前“工程风格”；以及 panel/dialog 中当字段之间有语义关联时，哪个参数组合更像用户想要的值。这些不是“合法性”问题，而是“**当前这位工程师在这一时刻更可能接受什么**”的问题。官方产品公开材料几乎都把这归入 next-edit/next-action 预测，而不是传统 completion。Copilot 文档、Microsoft Learn、Continue 文档都强调 next edit 依赖**recent edits / ongoing changes / current context**；Cursor 明确说 Tab 预测的是 **your next action across your codebase**，而不是单点补全。citeturn13view0turn13view2turn12view3turn14view0

但即使在这些场景里，我也不建议让 LLM 直接吐出高精度 PCB 坐标。更稳的做法是让模型只输出诸如：  
“建议类型 = component placement / via pattern / route guide / panel fill”；  
“锚点实体 = U7、J3、USB_DP/DM、GND ring”；  
“候选区域 = connector north side / keepout 外沿 / selected bus corridor”；  
“约束偏好 = 尽量短回路 / 保持与已选走线平行 / 避免穿越当前视口中心 / 遵循 high_speed netclass”；  
然后交给 deterministic geometry compiler 转成一个或几个 preview 候选。这样做的依据并不只是稳妥，而是与相邻成熟系统的 architecture 同构：Copilot 用 location model + edit model；Cursor 用专用低延迟 Tab 模型而不是通用 chat；KiCad/Altium 用规则驱动路由器而不是让用户逐点声明所有实际轨迹。citeturn13view1turn12view1turn11view1turn18view2

因此，对 KiSurf 最合适的分类不是“规则 vs 模型 二选一”，而是下面这种三层分工：

第一层是 **deterministic provider**，直接从 selection、tool state、rules、viewport、object topology、active dialog schema 里推出建议。第二层是 **algorithmic candidate generator**，在 constraint space 中生成几个可行解，比如几个不同的 route guide、几个可行 placement 点、几个 bus spacing 方案。第三层是 **ML/LLM ranker**，负责预测“现在给哪个建议，用户最可能接受，且最不打断工作流”。如果没有高置信度，系统就应该像 Cursor 所强调的那样，**宁可不建议**。citeturn14view0turn19view2turn19view3

## 推荐的 Provider Interface 与 Candidate Pipeline

我建议 KiSurf 把 Background Preview Agent 的核心抽象定义为 **provider chain + normalized candidate schema + calibrated ranker + confidence gate**，而不是“直接让某个模型决定要显示什么”。这个架构也最容易和你们现有的 typed ops、shadow board、preview scene、accept replay 基础设施对接。仓库 README 已经表明当前分支具备 typed atomic operations、native preview rendering、validation、accept replay，以及 model-facing tools 可以读取 context snapshots、workspace view、visual frame metadata/pixels、activity timeline、session control/query/render/validation；这意味着 next-action 系统根本不需要越过这些边界去碰 raw board internals，它只需要生产**previewable 的候选 bundle**即可。citeturn17view1

一个实用的 provider 接口可以是这种形态：给每个 provider 统一输入 `ObservationContext`，其中包含 board revision、selection revision、tool state、viewport、当前对话框 schema、近期 activity timeline、局部可视帧句柄、当前 DRC-lite 摘要、规则配置与当前工作模式；输出则是若干 `SuggestionCandidate`。候选对象不应该只是“文本解释”，而应当至少携带以下字段：

| 字段 | 作用 |
|---|---|
| `candidate_id` | 稳定标识，用于 supersede / reject / telemetry |
| `provider_kind` | rule / algorithm / model / hybrid |
| `action_kind` | place / route / fanout / fill / set-param / fix-drc / etc. |
| `anchor_entities` | pad / footprint / net group / zone / selected set / dialog fields |
| `region_hint` | 目标区域或 route corridor，而不是最终精确坐标 |
| `slot_values` | 已知参数、待推断参数、可选离散值或范围 |
| `preview_plan` | 生成 preview 所需的 typed operation bundle 或 parametric plan |
| `validation_summary` | DRC-lite、几何冲突、规则覆盖情况 |
| `risk_class` | low / medium / high，影响是否自动 ghost preview |
| `confidence` | 经过校准的 accept probability 或 utility score |
| `latency_class` | sync / fast-async / background |
| `staleness_key` | board/selection/tool/view/dialog 版本戳 |
| `explanation` | 面向用户的简述，不是必须但很重要 |

这里最重要的设计点有两个。第一，**所有 provider 的输出都先被“规范化”为统一 candidate schema**，这让 deterministic provider、solver provider、model provider 可以并行开发、并行触发、并由统一 ranker 排序。第二，`preview_plan` 不应该是“已经真改进 live board 的结果”，而应该是**一段能在 shadow board 上求值并在 native preview layer 上渲染的 typed plan**。这和 KiSurf 当前的 preview-first、typed atomic ops、accept replay 架构天然一致。citeturn17view1turn19view2turn19view3

基于公开 IDE 经验，我建议 provider chain 采用“**并行生成，统一裁决**”而不是“串行问一个又一个 provider”。VS Code 的扩展 API 明确支持多个 provider 并行返回结果并合并；inline completions 也会在用户停止输入后被调用，并携带 cancellation token，表明宿主本身就假设这类建议系统需要**快速取消、重新生成、并行比较**。对 KiSurf 来说，这意味着 deterministic provider、geometry solver provider、lightweight ranker 可以同时起跑；一旦上下文发生变化，就应像 VS Code 的 cancellation 一样立即废弃旧候选，而不是努力把旧候选修补到新上下文里。citeturn19view0turn19view2turn19view3

## 为什么不该让 LLM 直接输出高精度坐标

从架构上看，让 LLM 直接输出最终坐标、最终角度、最终弧线/走线节点，看似“端到端”，实际会同时破坏**稳定性、可解释性、重放性、校验可控性与交互质量**。GitHub 在 long-distance NES 中已经公开承认，当搜索空间扩展后，系统必须先解决“去哪”再解决“改什么”，否则错误跳转的成本过高；在 PCB 编辑里，这个问题只会更加严重，因为“去哪”不仅是哪个文件/哪一行，而是**哪个对象组、哪个拓扑局部、哪个允许区域、哪条规则边界**。同样，KiCad 与 Altium 的路由/差分对/多线 spacing 实践都说明：**具体几何应由尊重设计规则的求解器来生成**，而不是由高层语义系统直接决定。citeturn13view1turn11view1turn18view0turn18view2

因此，更稳定的模式是：  
模型输出“建议在 J3 到 U7 之间的当前可见 corridor 内，对 USB_DP/DM 做单步差分对继续布线，优先保持与现有段平行并减少 uncoupled fanout”；  
而不是输出“起点 (123.42, 88.19)，下一拐点 (126.85, 90.77) ……” 。  
前者可以被 deterministic compiler 编译成若干 route preview 候选，并通过 DRC-lite、gap check、clearance check 再筛一遍；后者一旦错了，系统几乎没有中间层可救。这个“intent → constrained candidate generation → ranking → preview”流，才是 EDA 里最稳的 AI 接入点。citeturn11view1turn18view2turn13view1

## 置信度门控与延迟预算该怎么设

Background Preview Agent 的 UX 成败，很大程度上取决于**什么时候提示、多久返回、多久不再打断**。人机交互里，Google 的 RAIL 模型建议用户输入触发的可见响应应在 **100ms** 内，让处理本身尽量在 **50ms** 内完成；Jakob Nielsen 的经典响应时间结论则指出 **0.1 秒**大致是“感觉系统立即响应”的界限，**1 秒**则是“仍保持思维流不断”的上限。把这些阈值和 Cursor 公开的 Tab p50 **260ms** 放在一起看，一个很现实的工程分层就出来了：**极快的同步 deterministic hint**、**稍慢但仍在工作流内的 fast predictive preview**、以及**更慢的后台候选预计算**。citeturn8search1turn8search14turn12view1

我建议 KiSurf 采用下面的延迟预算：

| 延迟层级 | 目标预算 | 适合的建议类型 | UI 呈现 |
|---|---|---|---|
| 即时层 | `<50ms` 计算，`<100ms` 可见响应 | schema 参数默认值、当前 net/layer/spacing 继承、极简单步 route/continue hint | 直接轻量 ghost / field prefill |
| 快速层 | `50–200ms` | 局部 placement 候选、单步 routing 候选、局部 DRC fix 候选、单个 bus routing 候选 | 原生 preview overlay，可 `Tab` 接受 |
| 工作流层 | `200ms–1s` | 多候选 route guide、局部 fanout 方案、多对象 placement 重排、复杂参数组合 | 只有上下文稳定时才显示；否则后台准备 |
| 后台层 | `>1s` | 跨区域建议、复杂 reroute、较重模型推理、跨 schematic/PCB 联动建议 | 不自动弹 ghost；仅 subtle badge / suggestion chip |

这样的分层与 IDE 现有实践是吻合的。VS Code 的 inline completion provider 明确是在“用户停止输入后”触发，并支持 cancellation；Cursor 把 next-action 预测做成高频、低延迟的专用模型；Continue 要求 Next Edit 使用专门模型并通过 diff overlays 呈现。这些都说明，**能进入工作区中央视觉层的建议，必须是低延迟且高置信度的**。citeturn19view0turn12view1turn12view3

置信度门控方面，我不建议只用“模型 softmax 分数”做阈值。更好的做法是学 Cursor 的思路，把门控目标定义成**expected utility**：建议被接受的概率、建议大小、风险类别、被接受后是否减少 DRC / 手工操作、是否会打断当前工具流，共同形成一个 utility score。Cursor 公开材料反复强调 accept rate，高 reject rate 会显著扰民；并且他们明确讨论了“何时啥也别显示”。因此对 KiSurf 来说，**“沉默能力” 是和“建议能力”同等重要的能力**。低风险、低成本、强上下文匹配的候选可以自动进入 ghost preview；中等置信度的候选只显示为轻量提示或侧边 suggestion chip；高风险或低置信度候选直接静默。citeturn14view0

## 如何评价 Suggestion 是否真的有用

最重要的线上指标不是“模型打了多少 token”，而是 suggestion 是否真的减少了工程师操作成本。Cursor 公布在线 RL 时，把**accept / reject**直接作为训练信号，并把“显示一个没被接受的建议”视为负回报；这说明在 next-action 产品里，**show rate 和 accept rate 必须一起看**。对 KiSurf 来说，单纯统计 accept 还不够，因为 EDA 里“接受后立刻撤销”“接受后立刻手工重做”“接受后引入新 DRC 问题”都不应算作真正成功。citeturn14view0

因此我建议把 usefulness metric 至少拆成四层。第一层是**呈现层指标**：展示率、可见时延、中断率、过期率、superseded 率。第二层是**交互层指标**：accept 率、reject/Esc 率、hover 查看率、`Tab` 接受率、time-to-accept。第三层是**质量层指标**：accept 后 N 秒内撤销率、accept 后重新编辑同一区域率、accept 后 DRC 改善率、accept 后 route completion 提升率、被接受候选的 stale-invalid accept 率。第四层是**任务层指标**：减少了多少点击、多少拖拽、多少手工布线长度、多少参数输入，是否缩短了完成某个布局/布线子任务的时间。Cursor 对 accept-rate 的强调与 Copilot/Continue 通过直观键盘接受形成的遥测闭环，都说明这些指标完全可以成为在线学习和阈值调节的基础。citeturn14view0turn13view0turn12view3

离线评估方面，我会建议用**回放日志**做 replay-based eval：给定历史 board/selection/tool/activity 序列，让 provider chain 在每个“可建议时刻”输出候选，然后比较历史用户后续实际动作与候选之间的匹配度。这样可以做 provider 级、候选级、ranker 级的离线回归测试，并且特别适合你们现有的 typed op / journal / replay 体系。由于 KiSurf README 已经表明当前分支具备 operation journal、checkpoints、rollback、native preview rendering、validation、accept replay，这条路在你们系统里不是额外负担，而是天然延伸。citeturn17view1

## 对 KiSurf 的推荐落地方案

结合你们当前仓库公开描述的能力边界，我的推荐是：

**Background Preview Agent 的 runtime 不直接调用 Chat Agent，也不直接运行自由 Python。**  
它应该建立在一个**专门的 next-action provider stack**之上，输出的是 reviewable suggestion candidates，而不是 open-ended script。Chat Agent 与 Python session runtime 保留给高自治任务；Background Preview Agent 则专注于低延迟、低风险、强上下文的建议。这跟 Cursor/JetBrains 的“Tab/next edit/agent mode 分层”完全一致，也更符合你们 README 里已经强调的 “Suggest, Accept, Materialize” 产品目标。citeturn20view0turn13view3turn17view1

一个适合 KiSurf 的长期稳定架构可以是：

| 层 | 角色 | 是否允许模型 |
|---|---|---|
| Observation Layer | 汇总 board/selection/tool/view/dialog/activity/vision/context 摘要 | 否 |
| Deterministic Providers | 参数补全、当前对象 defaults、局部 rule-based fix、简单 continue-route | 否 |
| Algorithmic Generators | placement 候选、route guide 候选、bus/diff pair/shape/via pattern 候选 | 否 |
| ML Ranker | 候选排序、是否显示、显示哪一种 UI、阈值校准 | 是，建议轻量专用模型 |
| LLM Semantic Provider | 仅在任务意图含糊或需要跨模态语义判断时参与；输出 anchor/constraints/ranking hints | 是，但不直接给最终几何 |
| Preview Compiler | 把候选编译成 typed preview bundle，跑 DRC-lite / 几何验证 | 否 |
| Accept Materializer | 用户接受后进入现有 preview-first → accept replay/commit 路径 | 否 |

在具体任务分类上，我会这样落锤：  
**表格填充建议、panel/dialog 参数补全** —— deterministic 为主，模型只做偏好排序；  
**器件/封装/过孔/形状放置建议** —— algorithm 生成候选，模型排序；  
**单步 routing 建议** —— deterministic router 为主，模型只决定是否值得弹出以及候选优先级；  
**bus routing / 多线等间距走线建议** —— deterministic multi-trace/route-guide solver 为主，模型做 net grouping、route corridor 选择和显示时机判定；  
**复杂跨区域、跨界面、跨 schematic/PCB 的语义建议** —— 才交给 LLM 参与，但输出仍然必须落回 candidate schema 与 typed preview plan。这个分配既遵循 IDE next-action 的多层模型设计，也遵循 EDA 里设计规则与几何求解器主导的传统。citeturn13view1turn14view0turn11view1turn18view2turn15search6

如果让我给一句最凝练的架构建议，那就是：

**用规则保证合法性，用算法构造候选，用模型决定是否打断用户以及优先展示哪个候选。**  
不要让 LLM 直接决定最终几何；不要把所有 next action 都塞进一个 provider；也不要让 Background Preview Agent 退化成低延迟版 Chat Agent。对 KiSurf 来说，最稳的长期设计是 **provider chain + candidate normalization + learned ranking + confidence gating + typed preview bundle**。这既与 GitHub Copilot/Cursor/JetBrains 的公开产品演进方向一致，也与 KiCad/Altium 这类 EDA 系统中“意图与规则求解分离”的基本工程现实一致。citeturn13view1turn14view0turn20view0turn11view1turn18view2turn17view1