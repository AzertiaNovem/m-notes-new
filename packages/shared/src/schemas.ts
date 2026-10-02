import { z } from "zod";

export const SEED_SUBJECTS = [
  "语文",
  "数学",
  "英语",
  "物理",
  "化学",
  "生物",
  "历史",
  "地理",
  "政治",
] as const;

export const SearchModeSchema = z.enum(["hybrid", "keyword", "rag"]);
export type SearchMode = z.infer<typeof SearchModeSchema>;

export const ChunkKindSchema = z.enum(["knowledge_point", "approach", "note"]);
export type ChunkKind = z.infer<typeof ChunkKindSchema>;

export const EntityKindSchema = z.enum(["note", "problem"]);
export type EntityKind = z.infer<typeof EntityKindSchema>;

export const NoteLinkViaSchema = z.enum(["manual", "embedding"]);
export type NoteLinkVia = z.infer<typeof NoteLinkViaSchema>;

export const SearchTargetSchema = z.enum(["all", "problems", "notes"]);
export type SearchTarget = z.infer<typeof SearchTargetSchema>;

export const TagViaSchema = z.enum(["exact", "alias", "auto_merged", "created"]);
export type TagVia = z.infer<typeof TagViaSchema>;

export const ChangeLogWriteSchema = z.object({
  editor_tool: z
    .string()
    .trim()
    .min(1)
    .max(64)
    .describe("编辑工具，如 mcp/cursor/claude/chatgpt/web"),
  change_summary: z
    .string()
    .trim()
    .min(1)
    .max(2000)
    .describe("变更大意：这次改了什么。必须由调用方填写，后端不会代写。"),
  changed_at: z
    .string()
    .trim()
    .min(1)
    .optional()
    .describe("可选 ISO 8601 时间；省略则服务器写入当前 UTC"),
});
export type ChangeLogWrite = z.input<typeof ChangeLogWriteSchema>;

export const ChangeLogEntrySchema = z.object({
  id: z.number().int(),
  editor_tool: z.string(),
  change_summary: z.string(),
  changed_at: z.string(),
});
export type ChangeLogEntry = z.infer<typeof ChangeLogEntrySchema>;

export const ChangeLogListResponseSchema = z.object({
  items: z.array(ChangeLogEntrySchema),
});
export type ChangeLogListResponse = z.infer<typeof ChangeLogListResponseSchema>;

export const DiagramInSchema = z.object({
  svg: z.string().min(1),
  caption: z.string().nullable().optional(),
});
export type DiagramIn = z.infer<typeof DiagramInSchema>;

export const DiagramOutSchema = z.object({
  id: z.number().int(),
  svg: z.string(),
  caption: z.string().nullable(),
  sort_order: z.number().int(),
});
export type DiagramOut = z.infer<typeof DiagramOutSchema>;

export const SolutionOutSchema = z.object({
  approach_md: z.string(),
  answer_md: z.string(),
});
export type SolutionOut = z.infer<typeof SolutionOutSchema>;

export const TagRefSchema = z.object({
  id: z.number().int(),
  name: z.string(),
  subject: z.string(),
  aliases: z.array(z.string()).default([]),
});
export type TagRef = z.infer<typeof TagRefSchema>;

export const TagResolutionSchema = z.object({
  input: z.string(),
  tag_id: z.number().int(),
  name: z.string(),
  via: TagViaSchema,
  score: z.number().nullable().optional(),
});
export type TagResolution = z.infer<typeof TagResolutionSchema>;

/** Canonical write payload. MCP `upsert_problem` and POST /api/v1/problems share this shape. */
export const ProblemCreateSchema = z.object({
  title: z.string().min(1),
  stem_md: z.string().min(1),
  diagrams: z.array(DiagramInSchema).default([]),
  subject: z.string().min(1),
  tags: z.array(z.string()).default([]),
  approach_md: z
    .string()
    .min(1)
    .describe(
      "解题思路：卷面之外的分析（审题、模型/方法选择、为什么、易错点）。不要写逐步计算或最终答案。",
    ),
  answer_md: z
    .string()
    .min(1)
    .describe("参考答案：卷面上的完整计算或推理过程，以及最终答案。"),
  source: z.string().nullable().optional(),
  mistake_note: z.string().nullable().optional(),
  difficulty: z.string().nullable().optional(),
  editor_tool: ChangeLogWriteSchema.shape.editor_tool,
  change_summary: ChangeLogWriteSchema.shape.change_summary,
  changed_at: ChangeLogWriteSchema.shape.changed_at,
});
export type ProblemCreate = z.input<typeof ProblemCreateSchema>;
export type ProblemCreateParsed = z.output<typeof ProblemCreateSchema>;

export const ProblemUpdateSchema = z.object({
  title: z.string().min(1).optional(),
  stem_md: z.string().min(1).optional(),
  diagrams: z.array(DiagramInSchema).optional(),
  subject: z.string().min(1).optional(),
  tags: z.array(z.string()).optional(),
  approach_md: z
    .string()
    .min(1)
    .optional()
    .describe("解题思路：卷面之外的分析。不要写逐步计算或最终答案。"),
  answer_md: z
    .string()
    .min(1)
    .optional()
    .describe("参考答案：完整计算或推理过程 + 最终答案。"),
  source: z.string().nullable().optional(),
  mistake_note: z.string().nullable().optional(),
  difficulty: z.string().nullable().optional(),
  editor_tool: ChangeLogWriteSchema.shape.editor_tool.optional(),
  change_summary: ChangeLogWriteSchema.shape.change_summary.optional(),
  changed_at: ChangeLogWriteSchema.shape.changed_at,
});
export type ProblemUpdate = z.infer<typeof ProblemUpdateSchema>;

export const ProblemSummarySchema = z.object({
  id: z.number().int(),
  title: z.string(),
  subject: z.string(),
  source: z.string().nullable(),
  difficulty: z.string().nullable(),
  tags: z.array(z.string()),
  created_at: z.string(),
  updated_at: z.string(),
});
export type ProblemSummary = z.infer<typeof ProblemSummarySchema>;

export const NoteEntityRefSchema = z.object({
  kind: EntityKindSchema,
  id: z.number().int(),
});
export type NoteEntityRef = z.infer<typeof NoteEntityRefSchema>;

export const RelatedEntitySchema = z.discriminatedUnion("kind", [
  z.object({
    kind: z.literal("problem"),
    id: z.number().int(),
    title: z.string(),
    subject: z.string(),
  }),
  z.object({
    kind: z.literal("note"),
    id: z.number().int(),
    title: z.string(),
    subject: z.string().nullable(),
  }),
]);
export type RelatedEntity = z.infer<typeof RelatedEntitySchema>;

export const NoteLinkSchema = z.object({
  id: z.number().int(),
  left: RelatedEntitySchema,
  right: RelatedEntitySchema,
  via: NoteLinkViaSchema,
  score: z.number().nullable(),
  label: z.string().nullable(),
});
export type NoteLink = z.infer<typeof NoteLinkSchema>;

export const RelatedHitSchema = z.object({
  entity: RelatedEntitySchema,
  score: z.number(),
  linked: z.boolean(),
});
export type RelatedHit = z.infer<typeof RelatedHitSchema>;

export const ProblemDetailSchema = ProblemSummarySchema.extend({
  stem_md: z.string(),
  mistake_note: z.string().nullable(),
  diagrams: z.array(DiagramOutSchema),
  solution: SolutionOutSchema,
  tag_resolution: z.array(TagResolutionSchema),
  links: z.array(NoteLinkSchema).default([]),
  related: z.array(RelatedHitSchema).default([]),
  change_logs: z.array(ChangeLogEntrySchema).default([]),
});
export type ProblemDetail = z.infer<typeof ProblemDetailSchema>;

export const ProblemListResponseSchema = z.object({
  items: z.array(ProblemSummarySchema),
  total: z.number().int(),
  limit: z.number().int(),
  offset: z.number().int(),
});
export type ProblemListResponse = z.infer<typeof ProblemListResponseSchema>;

export const SubjectCreateSchema = z.object({
  name: z.string().min(1),
});
export type SubjectCreate = z.infer<typeof SubjectCreateSchema>;

export const TaxonomyResponseSchema = z.object({
  subjects: z.array(z.string()),
  tags: z.array(TagRefSchema),
});
export type TaxonomyResponse = z.infer<typeof TaxonomyResponseSchema>;

export const SuggestTagsRequestSchema = z.object({
  subject: z.string().min(1),
  names: z.array(z.string()).default([]),
  name: z.string().optional(),
  limit: z.number().int().min(1).max(20).default(5),
});
export type SuggestTagsRequest = z.input<typeof SuggestTagsRequestSchema>;

export const TagMatchSchema = z.object({
  tag_id: z.number().int(),
  name: z.string(),
  score: z.number(),
  via: z.enum(["exact", "alias", "embedding", "substring"]),
});
export type TagMatch = z.infer<typeof TagMatchSchema>;

export const SuggestTagsResponseSchema = z.object({
  suggestions: z.array(
    z.object({
      input: z.string(),
      matches: z.array(TagMatchSchema),
    }),
  ),
});
export type SuggestTagsResponse = z.infer<typeof SuggestTagsResponseSchema>;

export const MergeTagsRequestSchema = z.object({
  source_tag_id: z.number().int(),
  target_tag_id: z.number().int(),
});
export type MergeTagsRequest = z.infer<typeof MergeTagsRequestSchema>;

export const MergeTagsResponseSchema = z.object({
  target: TagRefSchema,
  absorbed_name: z.string(),
  reindexed_problems: z.number().int(),
});
export type MergeTagsResponse = z.infer<typeof MergeTagsResponseSchema>;

export const DuplicatePairSchema = z.object({
  tag_a: TagRefSchema,
  tag_b: TagRefSchema,
  score: z.number(),
});
export type DuplicatePair = z.infer<typeof DuplicatePairSchema>;

export const DuplicatesResponseSchema = z.object({
  threshold: z.number(),
  pairs: z.array(DuplicatePairSchema),
});
export type DuplicatesResponse = z.infer<typeof DuplicatesResponseSchema>;

export const SearchRequestSchema = z.object({
  query: z.string().min(1),
  subject: z.string().nullable().optional(),
  tag: z.string().nullable().optional(),
  tag_id: z.number().int().nullable().optional(),
  mode: SearchModeSchema.default("hybrid"),
  target: SearchTargetSchema.default("all"),
  limit: z.number().int().min(1).max(100).default(20),
});
export type SearchRequest = z.input<typeof SearchRequestSchema>;
export type SearchRequestParsed = z.output<typeof SearchRequestSchema>;

export const SearchProblemSchema = z.object({
  kind: z.literal("problem"),
  id: z.number().int(),
  title: z.string(),
  subject: z.string(),
  source: z.string().nullable(),
  difficulty: z.string().nullable(),
  tags: z.array(z.string()),
  stem_md: z.string(),
});
export type SearchProblem = z.infer<typeof SearchProblemSchema>;

export const SearchNoteSchema = z.object({
  kind: z.literal("note"),
  id: z.number().int(),
  title: z.string(),
  subject: z.string().nullable(),
  body_md: z.string(),
});
export type SearchNote = z.infer<typeof SearchNoteSchema>;

export const SearchEntitySchema = z.discriminatedUnion("kind", [SearchProblemSchema, SearchNoteSchema]);
export type SearchEntity = z.infer<typeof SearchEntitySchema>;

export const SearchHitSchema = z.object({
  chunk_id: z.number().int(),
  kind: ChunkKindSchema,
  text: z.string(),
  score: z.number(),
  tag_id: z.number().int().nullable(),
  tag_name: z.string().nullable(),
  entity: SearchEntitySchema,
});
export type SearchHit = z.infer<typeof SearchHitSchema>;

export const SearchResponseSchema = z.object({
  query: z.string(),
  mode: SearchModeSchema,
  hits: z.array(SearchHitSchema),
});
export type SearchResponse = z.infer<typeof SearchResponseSchema>;

export const HealthResponseSchema = z.object({
  ok: z.boolean(),
  embedding_provider: z.string(),
  embedding_dim: z.number().int(),
  vec_enabled: z.boolean(),
  db_path: z.string(),
  tag_merge_threshold: z.number(),
  note_link_threshold: z.number(),
  print_ready: z.boolean(),
  print_layout_version: z.number().int().optional(),
});
export type HealthResponse = z.infer<typeof HealthResponseSchema>;

export const ProblemListQuerySchema = z.object({
  subject: z.string().optional(),
  tag: z.string().optional(),
  tag_id: z.coerce.number().int().optional(),
  limit: z.coerce.number().int().min(1).max(200).default(50),
  offset: z.coerce.number().int().min(0).default(0),
});
export type ProblemListQuery = z.input<typeof ProblemListQuerySchema>;

export const PrintBookCreateSchema = z.object({
  title: z.string().optional(),
  subjects: z.array(z.string()).default([]),
  tag_ids: z.array(z.number().int()).default([]),
});
export type PrintBookCreate = z.input<typeof PrintBookCreateSchema>;

export const PrintBookSummarySchema = z.object({
  id: z.number().int(),
  title: z.string(),
  subjects: z.array(z.string()),
  tag_ids: z.array(z.number().int()),
  page_count: z.number().int(),
  created_at: z.string(),
  updated_at: z.string(),
});
export type PrintBookSummary = z.infer<typeof PrintBookSummarySchema>;

export const PrintPageSummarySchema = z.object({
  page_no: z.number().int(),
  subject: z.string(),
  problem_ids: z.array(z.number().int()),
  titles: z.array(z.string()),
  stale: z.boolean(),
  sheet: z.number().int(),
});
export type PrintPageSummary = z.infer<typeof PrintPageSummarySchema>;

export const PrintBookDetailSchema = PrintBookSummarySchema.extend({
  pages: z.array(PrintPageSummarySchema),
});
export type PrintBookDetail = z.infer<typeof PrintBookDetailSchema>;

export const PrintPlanPageSchema = z.object({
  page_no: z.number().int(),
  subject: z.string(),
  problem_ids: z.array(z.number().int()),
  titles: z.array(z.string()),
  dirty: z.boolean(),
  blank: z.boolean(),
});
export type PrintPlanPage = z.infer<typeof PrintPlanPageSchema>;

export const PrintPlanSchema = z.object({
  book: PrintBookSummarySchema,
  first_generation: z.boolean(),
  page_count: z.number().int(),
  archived_page_count: z.number().int(),
  dirty_sheets: z.array(z.number().int()),
  new_sheets: z.array(z.number().int()),
  dirty_page_nos: z.array(z.number().int()),
  reprint_page_nos: z.array(z.number().int()),
  moved_problem_ids: z.array(z.number().int()),
  cost: z.number().int(),
  pages: z.array(PrintPlanPageSchema),
});
export type PrintPlan = z.infer<typeof PrintPlanSchema>;

export const PrintReprintSchema = z.object({
  page_nos: z.array(z.number().int().positive()).min(1),
});
export type PrintReprint = z.infer<typeof PrintReprintSchema>;

export const NoteNodeCreateSchema = z.object({
  title: z.string().min(1),
  body_md: z.string().default(""),
  subject: z.string().nullable().optional(),
  parent_id: z.number().int().nullable().optional(),
  sort_order: z.number().int().optional(),
  editor_tool: ChangeLogWriteSchema.shape.editor_tool,
  change_summary: ChangeLogWriteSchema.shape.change_summary,
  changed_at: ChangeLogWriteSchema.shape.changed_at,
});
export type NoteNodeCreate = z.input<typeof NoteNodeCreateSchema>;
export type NoteNodeCreateParsed = z.output<typeof NoteNodeCreateSchema>;

export const NoteNodeUpdateSchema = z.object({
  title: z.string().min(1).optional(),
  body_md: z.string().optional(),
  subject: z.string().nullable().optional(),
  parent_id: z.number().int().nullable().optional(),
  sort_order: z.number().int().optional(),
  editor_tool: ChangeLogWriteSchema.shape.editor_tool.optional(),
  change_summary: ChangeLogWriteSchema.shape.change_summary.optional(),
  changed_at: ChangeLogWriteSchema.shape.changed_at,
});
export type NoteNodeUpdate = z.infer<typeof NoteNodeUpdateSchema>;

export const NoteNodeUpsertSchema = z
  .object({
    id: z.number().int().optional(),
    title: z.string().min(1).optional(),
    body_md: z.string().optional(),
    subject: z.string().nullable().optional(),
    parent_id: z.number().int().nullable().optional(),
    sort_order: z.number().int().optional(),
    editor_tool: ChangeLogWriteSchema.shape.editor_tool,
    change_summary: ChangeLogWriteSchema.shape.change_summary,
    changed_at: ChangeLogWriteSchema.shape.changed_at,
  })
  .superRefine((value, ctx) => {
    if (value.id == null && !value.title) {
      ctx.addIssue({ code: "custom", message: "title is required when creating a note", path: ["title"] });
    }
  });
export type NoteNodeUpsert = z.input<typeof NoteNodeUpsertSchema>;

export const NoteNodeSummarySchema = z.object({
  id: z.number().int(),
  title: z.string(),
  subject: z.string().nullable(),
  parent_id: z.number().int().nullable(),
  sort_order: z.number().int(),
  created_at: z.string(),
  updated_at: z.string(),
});
export type NoteNodeSummary = z.infer<typeof NoteNodeSummarySchema>;

export type NoteTocNode = NoteNodeSummary & { children: NoteTocNode[] };

export const NoteTocNodeSchema: z.ZodType<NoteTocNode> = NoteNodeSummarySchema.extend({
  children: z.array(z.lazy(() => NoteTocNodeSchema)),
});

export type NoteTocReplaceNode = {
  id: number;
  children: NoteTocReplaceNode[];
};

type NoteTocReplaceNodeInput = { id: number; children?: NoteTocReplaceNodeInput[] };
export const NoteTocReplaceNodeSchema: z.ZodType<NoteTocReplaceNode, z.ZodTypeDef, NoteTocReplaceNodeInput> = z.object({
  id: z.number().int(),
  children: z.array(z.lazy(() => NoteTocReplaceNodeSchema)).default([]),
});

export const NoteTocReplaceSchema = z.object({
  nodes: z.array(NoteTocReplaceNodeSchema),
  editor_tool: ChangeLogWriteSchema.shape.editor_tool,
  change_summary: ChangeLogWriteSchema.shape.change_summary,
  changed_at: ChangeLogWriteSchema.shape.changed_at,
});
export type NoteTocReplace = z.infer<typeof NoteTocReplaceSchema>;

export const NoteMoveSchema = z.object({
  parent_id: z.number().int().nullable(),
  sort_order: z.number().int().optional(),
  editor_tool: ChangeLogWriteSchema.shape.editor_tool,
  change_summary: ChangeLogWriteSchema.shape.change_summary,
  changed_at: ChangeLogWriteSchema.shape.changed_at,
});
export type NoteMove = z.infer<typeof NoteMoveSchema>;

export const NoteNodeDetailSchema = NoteNodeSummarySchema.extend({
  body_md: z.string(),
  links: z.array(NoteLinkSchema),
  change_logs: z.array(ChangeLogEntrySchema).default([]),
});
export type NoteNodeDetail = z.infer<typeof NoteNodeDetailSchema>;

export const NoteWriteResponseSchema = z.object({
  node: NoteNodeDetailSchema,
  related: z.array(RelatedHitSchema),
  embedding_links: z.array(NoteLinkSchema),
});
export type NoteWriteResponse = z.infer<typeof NoteWriteResponseSchema>;

export const NoteLinkWriteSchema = z.object({
  from: NoteEntityRefSchema,
  to: NoteEntityRefSchema,
  label: z.string().nullable().optional(),
});
export type NoteLinkWrite = z.infer<typeof NoteLinkWriteSchema>;

export const NoteUnlinkSchema = z.object({
  from: NoteEntityRefSchema,
  to: NoteEntityRefSchema,
  via: NoteLinkViaSchema.optional(),
});
export type NoteUnlink = z.infer<typeof NoteUnlinkSchema>;

export const NoteLinkListQuerySchema = z.object({
  kind: EntityKindSchema.optional(),
  id: z.coerce.number().int().optional(),
});
export type NoteLinkListQuery = z.input<typeof NoteLinkListQuerySchema>;

export const NoteGraphNodeSchema = z.object({
  kind: EntityKindSchema,
  id: z.number().int(),
  title: z.string(),
  subject: z.string().nullable(),
});
export type NoteGraphNode = z.infer<typeof NoteGraphNodeSchema>;

export const NoteGraphEdgeSchema = z.object({
  from: NoteEntityRefSchema,
  to: NoteEntityRefSchema,
  via: NoteLinkViaSchema,
  score: z.number().nullable(),
  label: z.string().nullable(),
});
export type NoteGraphEdge = z.infer<typeof NoteGraphEdgeSchema>;

export const NoteGraphSchema = z.object({
  nodes: z.array(NoteGraphNodeSchema),
  edges: z.array(NoteGraphEdgeSchema),
});
export type NoteGraph = z.infer<typeof NoteGraphSchema>;

export const NoteDiffHunkKindSchema = z.enum(["equal", "add", "remove", "replace"]);
export type NoteDiffHunkKind = z.infer<typeof NoteDiffHunkKindSchema>;

export const NoteDiffHunkSchema = z.object({
  kind: NoteDiffHunkKindSchema,
  left: z.array(z.string()),
  right: z.array(z.string()),
});
export type NoteDiffHunk = z.infer<typeof NoteDiffHunkSchema>;

export const NoteVersionSourceSchema = z.enum(["log", "current", "empty"]);
export type NoteVersionSource = z.infer<typeof NoteVersionSourceSchema>;

export const NoteVersionRefSchema = z.object({
  id: z.number().int().nullable(),
  source: NoteVersionSourceSchema,
  editor_tool: z.string().nullable(),
  change_summary: z.string().nullable(),
  changed_at: z.string().nullable(),
  title: z.string(),
  parent_id: z.number().int().nullable(),
  sort_order: z.number().int(),
});
export type NoteVersionRef = z.infer<typeof NoteVersionRefSchema>;

export const NoteVersionSummarySchema = ChangeLogEntrySchema.extend({
  title: z.string().nullable(),
  parent_id: z.number().int().nullable(),
  sort_order: z.number().int().nullable(),
  has_snapshot: z.boolean(),
});
export type NoteVersionSummary = z.infer<typeof NoteVersionSummarySchema>;

export const NoteVersionSchema = NoteVersionSummarySchema.extend({
  body_md: z.string(),
});
export type NoteVersion = z.infer<typeof NoteVersionSchema>;

export const NoteVersionListResponseSchema = z.object({
  items: z.array(NoteVersionSummarySchema),
});
export type NoteVersionListResponse = z.infer<typeof NoteVersionListResponseSchema>;

export const NoteVersionDiffSchema = z.object({
  note_id: z.number().int(),
  from: NoteVersionRefSchema,
  to: NoteVersionRefSchema,
  title_changed: z.boolean(),
  parent_changed: z.boolean(),
  sort_changed: z.boolean(),
  hunks: z.array(NoteDiffHunkSchema),
});
export type NoteVersionDiff = z.infer<typeof NoteVersionDiffSchema>;
