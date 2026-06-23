# AI Native EDA 编辑器中表格与属性面板的 Background Preview Agent 设计

## 核心结论

对于 KiSurf 这类深度集成到 KiCad 内核的 AI Native EDA 编辑器，**表格和属性面板的 Background Preview Agent 不应被设计成“聊天式小助手”或“泛化 Python 执行器”**，而应被设计成一种**面向结构化 UI 的 typed suggestion engine**：它读取当前 panel/table 的语义状态，生成**可解释、可局部接受、可失效、可审计**的候选 patch，并以原生 preview 的方式显示在单元格、行、列、属性组或局部 panel 上；只有用户 Accept 后，才把这些 patch 转成 typed atomic operations 或 editor-native parameter updates。这个方向最符合 KiSurf 仓库当前强调的 “Suggest, Accept, Materialize”“preview-first”“semantic panel state”“observability logs”“background suggestions are preview-only” 的总体架构，也最符合 KiCad/Altium/Fusion/Onshape 这类 CAD/EDA 工具对参数、规则、单位、继承、验证和显式 Apply/Save 的长期工程习惯。citeturn17view0turn8view0turn7view1turn13view0turn9view0

从工程实践看，**表格/属性面板比 canvas 路由或自由几何更适合 deterministic-first 的建议体系**。KiCad 的 Net Classes 本身就是一个带单位、默认值、最小约束和规则覆盖关系的表；Altium Constraint Manager 是项目级、spreadsheet-like 的约束编辑器，支持 view-only、单位切换、默认值可视化、每格 comment、显式 save 才反映到设计、错误值高亮、undo/redo；Fusion 参数表支持名称、表达式、单位、注释、自动更新或暂停后 Apply；Onshape 的 feature dialog 把参数声明同时作为 UI schema 和 validity check，并支持 editing logic function 设智能默认值。也就是说，这类表面天然就不是“自由文本生成”问题，而是“带 schema 的补全、传播、推断和预览”问题。citeturn8view0turn7view1turn13view0turn13view1turn9view0

因此，**推荐的长期架构**是：
**typed panel context → deterministic/algorithmic candidate generation → optional model-assisted ranking or ambiguity resolution → preview patch layer → user accept/reject/modify → typed mutation journal**。模型不是第一作者，而是第二观察者、解释器和排序器。它不直接输出最终 cell text diff，更不直接越过 panel schema 改 live design；它最多输出高层意图、候选排序、缺失字段推断、命名建议或 explainability。这个分层与 VS Code inline completion 的 provider 机制、Copilot 的 ghost text/next edit suggestions、Excel Flash Fill 和 Google Sheets Smart Fill 的“先预览、后接受”交互也一致。citeturn10view0turn14view1turn11view0turn11view3turn7view2

## 现有工程实践给出的设计约束

KiSurf 仓库当前公开的 README 已经把几个关键架构方向说得很明确：项目目标是 AI-native、preview-first、one-action acceptance；当前分支已经有 Agent pane、semantic panel state、observability logs、Python-first execution session runtime、typed atomic operations、operation journal、checkpoints、rollback、validation、accept replay，而且背景建议在当前阶段是 **preview-only**，真正的 board mutation 要通过 accepted execution session。对于表格和属性面板，这意味着**不需要再造一条单独的“AI 特权写接口”**；更合理的做法是复用现有 suggestion/preview/journal 基础设施，只在上层新增 panel-surface adapter 和 panel-specific candidate providers。citeturn17view0

KiCad 的 Board Setup 已经说明：Net Classes 是一张规则表，每一类 net 有 copper clearance、track width、via sizes、differential pair sizes；更具体的规则可由 Custom Rules 覆盖，但不会突破 Constraints 中设置的最小值；Custom Rules 只有在语法无误时才会生效。这个体系说明在 EDA 里，面板里的值并不是“任意 editable string”，而是**受单位、最小值、默认值、覆盖优先级和语法规则共同约束的 typed field**。Background Preview Agent 若把建议建模成原始文本替换，未来一定会不断撞墙；若把建议建模成 typed patch，就能天然挂接验证器、规则检查器和 preview renderer。citeturn8view0

Altium 的 Constraint Manager 更像你要做的目标参考：它是 document-based、spreadsheet-like 的约束管理界面，可从 schematic 或 PCB editor 进入；支持 View Only 模式；支持单位切换；可显示默认值，默认值以灰色显示，自定义值以白色显示；支持复制粘贴、搜索、每个约束单元的 comment；需要 Save 后才反映到设计；错误的约束定义会在表格里高亮为红色；大多数区域支持 undo/redo。这个产品已经证明，**一个真正可用的 EDA 约束表面必须具备 schema、继承、可视状态、延迟生效和错误高亮**，而不是简单的“算出推荐值然后直接填进去”。citeturn7view1

Fusion 的参数系统给了另一个强烈信号：参数表有 Name、Expression、Unit、Comment；编辑时可以选择 Automatic Update，也可以关掉自动更新以减少 compute time，然后再 Apply；参数更新会传播到引用它的设计；用户还能在其他参数输入框中直接复用已有参数名和表达式。这说明属性面板建议不应只做“值补全”，还应支持**表达式级建议、单位转换建议、引用已有 project symbol 的建议，以及“预览但延迟求值/更新”的模式**。citeturn13view0turn13view1turn13view2

Onshape 的 Feature UI 进一步说明，好的 CAD 参数面板不是纯静态表单：参数声明既是 UI 定义也是 validity check，支持数量和单位、表达式、lookup table、条件显示、参数分组，以及 editing logic function 在用户编辑过程中智能设默认值；但官方文档也明确提醒，editing logic 很容易被“用过头”，造成比帮助更多的打扰。这个警示对 Background Preview Agent 很关键：**建议系统必须低打扰，必须偏向“可见但不夺权”的默认行为。**citeturn9view0

IDE 与电子表格两侧也给出同样的交互参考。GitHub Copilot 的 inline suggestions 以 dimmed ghost text 展示，支持 partial accept；next edit suggestions 会根据当前正在进行的编辑预测下一个位置和修改，并允许通过 `Tab` 导航和接受；VS Code 的 InlineCompletion API 允许多个 provider 并行返回结果并合并，provider 通过 cancellation token 响应上下文变化；Excel Flash Fill 会在识别到列模式后显示整列 preview，用户按 Enter 接受；Google Sheets Smart Fill 会检测模式并给出建议，增强版会用 AI 预测剩余值；Gemini in Sheets 还支持在 side panel 中做 fill range、set number format、insert dimension、filter/sort，并附带 sources、retry 与好/坏建议反馈。对于 KiSurf 来说，这些实践共同指向：**表格建议要先预览，再局部接受；要允许 provider 并行、快速失效、来源可见、反馈可收集。** citeturn14view1turn14view0turn10view0turn10view1turn11view0turn11view3turn7view2turn18view0

## 推荐的数据模型

如果要让 Background Preview Agent 在表格和属性面板中长期稳定工作，核心不是“让模型看一眼 UI 截图然后补几个格子”，而是先把 panel surface 建模成一个**稳定的、可复用的上下文协议**。建议你把每一次建议生成的输入都规范成 `PanelContext`，其中至少包含：当前 editor 类型、panel id、table id、schema version、focused cell、selected range、visible columns、sort/filter 状态、每个 cell 的 typed value 与 display value、该值是否 inherited/default/read-only、可选枚举、单位、验证状态、错误状态、以及与 project context 的关联键。这个思路和 Onshape 把参数声明同时当作 UI/validation schema、Fusion 把参数表作为表达式/单位编辑器、Altium 把约束矩阵当作正式项目文档、KiSurf 把 semantic panel state 作为 agent 可见状态，是完全一致的。citeturn9view0turn13view0turn7view1turn17view0

一个可落地的 `PanelContext` 可以长成这样：

```ts
type PanelContext = {
  surface: {
    editor: "pcb" | "schematic" | "rule_editor" | "property_panel";
    panelId: string;
    tableId?: string;
    schemaRev: string;
    panelRev: string;
  };
  focus: {
    cell?: { rowKey: string; colKey: string };
    selection?: { rows: string[]; cols: string[]; ranges: CellRange[] };
    visibleWindow?: CellRange;
  };
  rows: Array<{
    rowKey: string;
    objectRef?: string;          // netclass / net / footprint / via-template / rule-id
    cells: Record<string, CellState>;
  }>;
  options: {
    unitSystem: "mm" | "mil";
    sortState?: unknown;
    filterState?: unknown;
    editMode?: "view" | "edit" | "preview";
  };
  project: {
    boardRev: string;
    rulesRev: string;
    selectionRev: string;
    namingProfileRev: string;
    linkedObjects?: string[];
  };
};
```

这里最重要的不是字段多少，而是**每个字段都必须服务于“候选生成”“stale 判定”“preview 渲染”“accept replay”“审计解释”**。例如 `boardRev/rulesRev/selectionRev` 是为了做 suggestion validity；`objectRef` 是为了把属性面板和真实 netclass、footprint、rule、zone 等对象绑定起来；`inherited/default` 是为了把建议渲染成“覆盖值”还是“显式补全值”；`validationState` 是为了把 panel preview 直接关联到 KiCad/EDA 规则系统，而不是事后出错才知道。KiCad netclass 的默认/覆盖关系、Altium 的默认值显示和错误约束红色高亮、Fusion 的 expression/value 区分，都支持这个设计方向。citeturn8view0turn7view1turn13view0turn13view1

在这个上下文之上，建议不要让 provider 直接返回“字符串替换”，而应返回**typed candidate schema**。候选至少要分成几类：`cell_patch`、`row_patch`、`column_fill`、`selection_fill`、`property_override`、`format_patch`、`comment_patch`、`rule_binding`。每个候选都要带 target range、typed value、display preview、confidence、reason、source provider、依赖的上下文 revision、影响范围、潜在 validation warnings、以及 accept 后要下沉成的 atomic op 类型。KiSurf 现有 README 已经强调 typed atomic operations、typed properties、property patches merge into shadow-board state、accepted property changes 用 native setters 而不是几何 patching；把 panel suggestion 也跟这套 typed journal 对齐，是最自然也最便宜的路径。citeturn17view0

```ts
type SuggestionCandidate = {
  kind:
    | "cell_patch"
    | "row_patch"
    | "column_fill"
    | "selection_fill"
    | "property_override"
    | "format_patch"
    | "comment_patch";
  target: CellTarget | RowTarget | ColumnTarget | RangeTarget;
  patch: TypedValuePatch[];
  confidence: number;
  reason: string[];
  provenance: {
    provider: string;
    sourceRows?: string[];
    sourceObjects?: string[];
    modelPromptRef?: string;
  };
  validity: {
    boardRev: string;
    rulesRev: string;
    panelRev: string;
    selectionRev?: string;
    focusRev?: string;
  };
  checks?: ValidationMessage[];
};
```

对于用户提到的 project context，建议也不要把整个 board 全量塞给面板 Agent，而是给一个 **panel-relevant project slice**。例如在 netclass 表里，只需要相关 netclass、基线约束、最近命名习惯、有关 nets 的分布统计和最近修改历史；在 footprint 属性面板里，只需要当前对象、类别邻域、匹配的封装模板、grid/placement room、相关规则引用。KiSurf README 已经强调 AI 应基于 schematic、PCB、rules、footprints、design history、current task 等实时上下文工作，但“实时”不等于“全量”；对于表格建议，**局部且结构化的上下文比整板截图更有用。** citeturn17view0

## 候选生成与排序架构

这类表格/属性面板的 next-action 生成，最优解不是“全规则”也不是“全模型”，而是**deterministic provider + algorithmic provider + model-assisted reranker 的混合架构**。VS Code 的官方 API 已经证明，多 provider 并行返回、结果合并、失败隔离、基于 cancellation token 取消，是一种稳定而成熟的宿主模式；Copilot 的 next edit suggestions 也本质上是在已有编辑流上预测下一个局部修改，而不是接管整个文档。KiSurf 若把这套模式搬到 panel world，会比“一个大模型直接决定所有 cell”稳健得多。citeturn10view0turn10view2turn14view1

建议把 provider 分成三层。

**第一层是 deterministic provider**，默认同步执行，目标延迟在几十毫秒量级，只做高置信、零歧义、强 schema 约束的建议。这一层应覆盖：fill-down、same-as-above、copy selection pattern、等差/等比序列、单位归一、enum/domain 自动补全、netclass default 继承、由当前行向同类行传播规则、对称矩阵自动补全、命名后缀递增、属性表里由对象 class 决定的默认值补全。KiCad netclass 的默认/覆盖关系、Altium 的默认值显示、Fusion/Onshape 的 typed parameter + unit/expression 体系，都说明这些建议不需要 LLM。citeturn8view0turn7view1turn13view0turn13view1turn9view0

**第二层是 algorithmic provider**，可在 50–200ms 的预算内异步完成，负责比规则稍复杂、但仍然可以显式求解的候选生成。这一层适合做：规则矩阵传播、同类对象聚类后的批量参数补全、单位换算和工程表达式规范化、跨行依赖解析、命名模板匹配、候选范围筛选、以及由已有 board/project statistics 推出的默认值推荐。Google Sheets Smart Fill 和 Excel Flash Fill 的模式识别，本质上就属于规则/算法优先的范式；它们之所以可用，是因为输出是结构化表面上的模式完成，而不是开放域写作。citeturn11view0turn11view3turn7view2

**第三层才是 model-assisted provider**，而且它最好只做三件事：歧义消解、候选排序、解释生成。模型适合处理的，是“这个新 netclass 更像 USB_HS 还是 CAM_MIPI？”“当前器件属性表中这几列剩余值应该跟已有命名惯例保持哪种风格？”“这个 panel 上用户正在做的是 impedance setup 还是 generic clearance cleanup？”这类需要 project semantics、历史习惯和当前意图的判断。Google Sheets 的 enhanced Smart Fill 和 Gemini in Sheets 都说明，AI 在结构化表面上最有价值的是**预测 incomplete column relationships、根据 prompt 做高层操作和给出可追溯来源/反馈回路**，而不是绕过表格 schema 直接乱写。citeturn7view2turn18view0turn11view3

因此，建议把 provider pipeline 明确写成：

```text
PanelContext
  → schema validator
  → deterministic providers
  → algorithmic candidate generators
  → candidate normalizer
  → optional model ranker / disambiguator
  → confidence gating
  → preview renderer
```

在这个 pipeline 里，**模型输入最好不是 raw table dump，而是“已生成候选 + panel summary + project slice + user recent actions”**。模型输出也不应是 raw cell diff，而应是 `choose(candidate_ids)`、`rerank(candidate_ids)`、`explain(candidate_id)`、`refine_missing_fields(candidate_template)` 这类更受控的结果。这样做可以避免 LLM 直接输出高精度数值、错误单位、越权填表和不合 schema 的 patch，也更符合 KiSurf 当前“typed atomic operations + guarded semantic UI actions + preview-first” 的总体边界。citeturn17view0

对于置信度阈值，建议采用三段式。高置信 deterministic/algorithmic 建议可以直接以 ghost preview 显示；中等置信建议以弱提示加 reason chip 显示，默认不抢焦点；低置信或高影响范围建议只在 side gutter / lightbulb / panel header 中提示“有建议可查看”，不直接铺进表格。这个分层与 Copilot 的 ghost text、next edit suggestion gutter arrow、Excel Flash Fill 整列预览、Fusion 的 Automatic Update / Apply 分离很一致：**越局部、越高置信，展示越直接；越大范围、越高代价，越应显式确认。** citeturn14view0turn7view0turn11view0turn13view1

## Preview、失效与接受交互

在表格里，preview 最好不是弹窗，也不是聊天文本，而是**原位 ghost preview**。推荐的默认视觉语言是：ghost value、轻量背景 tint、左上角或右侧 reason badge、必要时附一个 source icon。对于 inherited/default 类建议，视觉上可以借鉴 Altium 的“默认值灰色、自定义值白色”；对于错误或冲突，可以借鉴 Altium 的红色错误值高亮和 KiCad Custom Rules 的 syntax-check-before-apply 思路；对于说明性建议，可以借鉴 Fusion 的 comment 字段与 Google Gemini/Sheets 的 visible sources。也就是说，preview 不只告诉用户“建议什么”，还要告诉用户“来自哪里、会覆盖什么、为什么现在给你看”。citeturn7view1turn8view0turn13view0turn18view0

建议把 suggestion 的失效分成 **hard stale** 和 **soft stale**。
Hard stale 的典型触发包括：目标 cell 被用户改过、目标 row/column 被删除、selection 改变且 suggestion 绑定的是 selection、属性面板绑定对象切换、schema/version 改变、单位模式切换导致 preview 语义变化、base board/rules hash 变化且触及 suggestion 依赖对象。Soft stale 则是：panel 外发生了不相关编辑，或 project context 统计变了但 target 仍存在。这时不必立刻丢弃 suggestion，而应做一次快速 revalidate。VS Code inline completion 在 selected completion item 变化时会重新请求 provider，provider 方法也都带 cancellation token；KiSurf 当前 README 也明确 stale accept 会因为 base hash 而被拒绝。把 panel suggestion 也做成 revision-bound object，可以让你自然得到同样的失效保障。citeturn10view1turn10view0turn17view0

建议把每个 suggestion 绑定一个 validity tuple，例如：`{boardRev, rulesRev, panelRev, schemaRev, selectionRev, focusRev, targetCellHash[]}`。当用户继续编辑时，系统只检查与该 tuple 相关的 revision 是否变化，而不是笼统地“任何变化都清空”。这会让 Background Preview Agent 更稳：如果用户只是平移视图、切到别的 tab 再回来、或改了与当前 panel 无关的 board 对象，合理的建议应尽量保留；如果用户在同一张表里改了目标列或切换了 object binding，则应立即撤掉。Copilot 的 next edit suggestion 本来就是依赖 ongoing changes 预测“下一个相关位置”，不是做永恒 suggestion；用于 EDA panel 时，同样应是**强上下文绑定、快速重算、避免陈旧覆盖**。citeturn14view1turn10view2

Accept 语义上，表格不该只有 “accept all” 一种模式。最好的层级是：**accept cell、accept contiguous range、accept row、accept selected rows、accept column propagation、accept all visible preview**。这其实对应了三类已经被验证的交互：Copilot 的 partial accept，Excel Flash Fill 的整列接受，以及 CAD 参数面板的单格/单参数编辑后 Apply。对于 KiSurf 来说，最自然的键盘语义通常是：
当前 cell 上的高置信 ghost preview → `Tab` 接受当前 cell；
当前 selection 上的同质 preview → `Alt+Tab` 或 `Ctrl+Enter` 接受 selection；
面板顶部 summary suggestion → 点击或 `Ctrl+Shift+Enter` 接受 all。
这样能明显降低误触风险，避免因为表格天然用 `Tab` 切列而导致“自动接受整片 patch”的灾难。GitHub Copilot 之所以能让 `Tab` 接受，是因为它面对的是单光标或单建议点；表格里必须更保守。citeturn14view0turn11view0turn13view1

Reject 和 modify preview 也应是一等公民。建议用户可以直接覆盖 ghost value 输入自己的值；系统把这看作“accept candidate with local override”，而不是“先拒绝 AI 再重新手输”。Google Gemini in Sheets 已经把 retry、insert、good/bad suggestion 做成 side-panel 交互；Altium 则允许 comment、undo/redo、显式 save。把这些经验综合一下，KiSurf 表格 Agent 最合理的模型不是二元的 accept/reject，而是四元的：**accept、accept-partial、modify-and-accept、dismiss**。citeturn18view0turn7view1turn14view0

一个实用的 UX 规则是：**默认只保留一个 active suggestion cluster**。在画布上多 suggestion 也许还能容忍，但在高度信息密集的约束表里，同时漂浮多组 ghost values 很快会让用户迷失。更好的做法是：当前 focus/selection 相关的 suggestion 作为 active cluster 原位显示；其他候选只在 panel header 的 lightbulb、side gutter 或 “N more suggestions” 抽屉里列出。VS Code 的 next edit suggestion 会通过 gutter arrow 指示其他位置有 suggestion；Fusion 参数表通过 filters/favorites/search 缩小可见面；这些都是“压低认知噪声”的成熟做法。citeturn14view0turn13view1turn7view0

## 推荐的 UI/UX 与 provider pipeline 组合

从产品层面，我会把表格/属性面板的 Background Preview Agent 分成四种 suggestion mode，而不是一刀切。

**Silent assist**：只做 autocomplete、enum/domain 补全、单位规范化、显式默认值提示。这类建议几乎零风险，应尽量接近普通编辑器的 inline assist。Google Sheets 的 autocomplete/Smart Fill、Copilot 的 ghost text 都属于这一类。citeturn11view3turn14view1

**Inline preview**：对单 cell、同一 selection、同一行中的几个相关字段显示 ghost values。这适合 fill-down、rule propagation、同类 netclass 参数补全、器件属性表中命名/封装规则补齐。此时必须给出 reason，例如“Derived from selected rows”“Inherited from netclass default”“Converted from mil to mm”。Altium 的默认值显示、Fusion 的表达式/值切换、Onshape 的 editing logic 都提供了很好的参考。citeturn7view1turn13view0turn9view0

**Panel-level suggestion**：当建议涉及多行多列、清洗整个规则表、统一命名、批量格式转换、生成一组 dropdown/checkbox/format 之类更大动作时，不应直接把整个表都 ghost 化，而应在 panel header 生成 summary card：说明将影响哪些范围、依据什么模式、有哪些 warnings。Google Gemini in Sheets 已经把 fill range、set number format、insert dimension、multi-step tasks 放到了 side panel，而不是直接在每个格里神出鬼没地写字；EDA 工具里更应如此。citeturn18view0

**Ask-to-preview**：对于低置信或高影响的建议，只显示“有建议”而不直接预览，例如“根据现有高速差分对习惯，我可以补完这一页约束表”。这类流程更接近 lightbulb，而不是 ghost text。对 EDA 来说，这一点尤其重要，因为错误的规则传播可能比错误的代码补全更昂贵。KiSurf README 也反复强调硬件正确性优先、建议需要可检查、可逆。citeturn17view0

基于这些模式，provider pipeline 我建议做成下面这组接口：

```ts
interface PanelSuggestionProvider {
  id: string;
  kinds: SuggestionKind[];
  canHandle(ctx: PanelContext): boolean;
  estimateCost(ctx: PanelContext): "sync" | "fast-async" | "slow-async";
  propose(ctx: PanelContext, token: CancellationToken): Promise<SuggestionCandidate[]>;
}

interface PanelSuggestionRanker {
  rank(ctx: PanelContext, candidates: SuggestionCandidate[]): Promise<RankedCandidate[]>;
}

interface PanelSuggestionGate {
  decide(ctx: PanelContext, ranked: RankedCandidate[]): DisplayDecision;
}
```

这里的关键不是代码形式，而是职责边界：
provider 只负责提出候选；
ranker 只负责排序和解释；
gate 决定是否展示、以何种方式展示、是否允许快捷键接受。
这种拆分和 VS Code 的多 provider 合并模式高度一致，也能让 deterministic provider 和 model provider 并存。citeturn10view0turn7view0

## 测试方法与落地路径

第一阶段测试不要从“模型是否聪明”开始，而要从**proposal surface 是否稳定**开始。你需要先做 schema/golden tests：给定一个 panel snapshot，deterministic provider 应该稳定地产生相同的 typed candidates；变更 unit system、默认值来源、最小约束、列顺序或筛选条件后，输出应按预期变化。KiCad netclass 的单位和最小约束、Altium 的默认值可视化与错误高亮、Fusion 的 expression/unit/comment 模型，都很适合作为黄金样例来源。citeturn8view0turn7view1turn13view0turn13view1

第二阶段要做 **stale/invalidity tests**。你需要系统性地回放这些场景：建议出现后用户修改目标 cell；用户换 selection；属性面板绑定对象改变；panel schema 升级；单位从 mm 切到 mil；board/rules revision 变化但 target 未变；board/rules revision 变化且 target 相关对象变化。预期行为必须可预测：有的 suggestion 立即消失，有的被自动重算，有的保留但降级为 panel-level hint。VS Code provider 的 cancellation token、selected item change re-request，以及 KiSurf 对 stale accept 的 base hash 拒绝机制，都说明这类测试必须从一开始就写。citeturn10view0turn10view1turn17view0

第三阶段是 **accept/replay/audit tests**。每一次 accept 都应该产出明确的 typed journal：谁给的建议、建议作用于哪些 cell、用户接受了哪些子集、是否发生 override、最终落到哪些 atomic ops 或 panel property setters。KiSurf 仓库已经有 observability log、typed operation journal、typed property patches、accept replay 这些基础；表格/属性面板只要在同一条审计链路里增加 `panelSuggestionId` 和 `acceptedPatchIds` 即可。这样你才能做离线重放、误建议分析、provider 质量比较和用户反馈闭环。citeturn17view0

第四阶段才是 **质量与产品指标**。我建议至少跟踪：suggestion surfaced rate、dismiss rate、accept rate、accept-partial rate、modify-after-preview rate、undo-after-accept rate、stale-drop rate、median preview latency、p95 preview latency、以及“被用户继续编辑覆盖”的比例。Google Gemini in Sheets 明确提供 Good/Bad suggestion 反馈与 Sources 展示；KiSurf 现有 observability log 已能记录 user input、model I/O、tool calls、suggestions 与 semantic panel state。只要把 panel suggestion 也接入这条数据链路，你就会拥有非常强的产品迭代抓手。citeturn18view0turn17view0

从落地顺序看，我建议 MVP **只做 deterministic-first 的 panel preview agent**，不要一开始就把 LLM 放到主通路里。最先做的三类 surface，应该是：
一类是 **KiCad 风格的 netclass/rule tables**，因为 schema 清晰、项目上下文强、验证器现成；
一类是 **对象属性面板**，因为单对象 scope 小、容易做局部 preview 与 stale 判断；
一类是 **参数表/命名表**，因为适合验证 fill-down、单位转换、命名风格补全等高频模式。KiCad/Altium/Fusion/Onshape 这些成熟产品都说明，越是参数化和规则化的表面，越适合作为智能建议的第一批着陆点。citeturn8view0turn7view1turn13view0turn9view0

长期来看，最稳妥的终局不是“让模型理解每个表格长什么样”，而是让所有表格和属性面板都实现同一个宿主协议：`PanelContextProvider + PanelPreviewRenderer + PanelMutationJournalAdapter`。一旦这个宿主协议稳定了，你后续无论接 deterministic providers、启发式 solver、项目统计模型、还是 LLM ranker，都不需要重写底层。这个方向与 KiSurf 当前 README 里已经出现的 semantic panel state、preview-first、typed ops、background preview-only suggestions 完全同向。citeturn17view0

## 最终推荐方案

**推荐的产品与架构判断很明确：**

表格和属性面板的 Background Preview Agent，应该以**结构化建议系统**而不是**自由脚本代理**来设计。它的核心输出不是自然语言，也不是 Python cell，而是**面向 panel schema 的 typed preview patches**。这些 patches 从 deterministic providers 起步，必要时再经过算法候选生成和模型排序。这样既能对齐 KiSurf 已有的 typed atomic ops / preview-first / journal / observability，又能继承 KiCad、Altium、Fusion、Onshape 这些 CAD/EDA 参数表面的长期稳定范式。citeturn17view0turn8view0turn7view1turn13view0turn9view0

**推荐的 UI/UX** 是：
默认原位 ghost preview；
局部 accept 为主，accept all 为辅；
reason/source 可见；
高影响建议先 summary card 再 preview；
stale suggestion 基于 revision tuple 自动失效或重算；
用户可以 modify-and-accept，而不是只能二选一。
这套体验同时吸收了 Copilot ghost text/partial accept/next edit、Excel Flash Fill preview-and-accept、Google Sheets Smart Fill/Gemini side panel、以及 Altium/Fusion 的显式 Apply/Save/Undo 逻辑。citeturn14view0turn14view1turn11view0turn11view3turn18view0turn7view1turn13view1

**推荐的 provider pipeline** 是：
deterministic providers 负责 fill-down、继承、单位、默认值、矩阵传播；
algorithmic providers 负责聚类、模式检测、跨行依赖；
model 只负责歧义消解、排序、解释和高层意图落地。
不要让模型直接输出 raw cell diffs，更不要让它绕过 panel schema 或直接写 live project state。对表格和属性面板而言，这不是保守，而是长期可扩展性的前提。citeturn10view0turn17view0turn8view0turn9view0

如果只给一句最浓缩的结论，那就是：

**把表格和属性面板当成“带 schema 的可验证编辑面”，而不是“缩小版聊天窗口”。**
这样设计出来的 Background Preview Agent，才会像真正的 EDA 工程协作者，而不是一个偶尔能补对几个格子的外挂。citeturn17view0turn7view1turn8view0