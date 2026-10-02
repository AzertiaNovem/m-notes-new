import { useEffect, useMemo, useRef, useState } from "react";
import { useNavigate } from "react-router-dom";
import type { NoteGraph, NoteGraphEdge, NoteGraphNode } from "@mistakebook/shared";
import { api } from "../api.ts";

type SimNode = NoteGraphNode & { x: number; y: number; vx: number; vy: number };

function keyOf(kind: string, id: number) {
  return `${kind}:${id}`;
}

export function GraphPage() {
  const navigate = useNavigate();
  const [graph, setGraph] = useState<NoteGraph | null>(null);
  const [error, setError] = useState("");
  const [includeProblems, setIncludeProblems] = useState(true);
  const [nodes, setNodes] = useState<SimNode[]>([]);
  const wrapRef = useRef<HTMLDivElement | null>(null);
  const [size, setSize] = useState({ w: 720, h: 480 });

  useEffect(() => {
    api
      .getNoteGraph(includeProblems)
      .then(setGraph)
      .catch((err) => setError(err instanceof Error ? err.message : String(err)));
  }, [includeProblems]);

  useEffect(() => {
    const el = wrapRef.current;
    if (!el) return;
    const sync = () => setSize({ w: Math.max(320, el.clientWidth), h: Math.max(360, el.clientHeight) });
    sync();
    const observer = new ResizeObserver(sync);
    observer.observe(el);
    return () => observer.disconnect();
  }, []);

  const initial = useMemo(() => {
    if (!graph) return [];
    const cx = size.w / 2;
    const cy = size.h / 2;
    const n = Math.max(graph.nodes.length, 1);
    return graph.nodes.map((node, index) => {
      const angle = (2 * Math.PI * index) / n;
      const radius = Math.min(size.w, size.h) * 0.32;
      return {
        ...node,
        x: cx + Math.cos(angle) * radius,
        y: cy + Math.sin(angle) * radius,
        vx: 0,
        vy: 0,
      };
    });
  }, [graph, size.h, size.w]);

  useEffect(() => {
    if (!graph || !initial.length) {
      setNodes([]);
      return;
    }
    let current = initial;
    setNodes(current);
    let frame = 0;
    let ticks = 0;
    const tick = () => {
      current = step(current, graph.edges, size.w, size.h);
      setNodes(current);
      ticks += 1;
      const energy = current.reduce((sum, node) => sum + Math.abs(node.vx) + Math.abs(node.vy), 0);
      if (ticks < 240 && energy > 0.4) frame = requestAnimationFrame(tick);
    };
    frame = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(frame);
  }, [graph, initial, size.h, size.w]);

  return (
    <section>
      <div className="page-head">
        <h1>图谱</h1>
        <p className="muted">实线是手工边，虚线是 embedding 边。</p>
      </div>
      <label className="chip">
        <input
          type="checkbox"
          checked={includeProblems}
          onChange={(e) => setIncludeProblems(e.target.checked)}
        />
        显示关联题目
      </label>
      {error ? <p className="error">{error}</p> : null}
      {graph && !graph.nodes.length ? <p className="empty">还没有笔记或边。</p> : null}
      <div className="graph-wrap" ref={wrapRef}>
        <svg className="note-graph" viewBox={`0 0 ${size.w} ${size.h}`} role="img">
          {graph?.edges.map((edge, index) => {
            const a = nodes.find((n) => n.kind === edge.from.kind && n.id === edge.from.id);
            const b = nodes.find((n) => n.kind === edge.to.kind && n.id === edge.to.id);
            if (!a || !b) return null;
            return (
              <line
                key={`${keyOf(edge.from.kind, edge.from.id)}-${keyOf(edge.to.kind, edge.to.id)}-${edge.via}-${index}`}
                x1={a.x}
                y1={a.y}
                x2={b.x}
                y2={b.y}
                className={edge.via === "embedding" ? "graph-edge embedding" : "graph-edge manual"}
              />
            );
          })}
          {nodes.map((node) => (
            <g
              key={keyOf(node.kind, node.id)}
              className="graph-node"
              transform={`translate(${node.x},${node.y})`}
              onClick={() => navigate(node.kind === "note" ? `/notes/${node.id}` : `/problems/${node.id}`)}
            >
              <circle r={node.kind === "note" ? 14 : 11} className={node.kind} />
              <text y={26} textAnchor="middle">
                {node.title.length > 8 ? `${node.title.slice(0, 8)}…` : node.title}
              </text>
            </g>
          ))}
        </svg>
      </div>
    </section>
  );
}

function step(nodes: SimNode[], edges: NoteGraphEdge[], width: number, height: number): SimNode[] {
  const next = nodes.map((node) => ({ ...node }));
  const byKey = new Map(next.map((node) => [keyOf(node.kind, node.id), node]));
  const cx = width / 2;
  const cy = height / 2;
  for (let i = 0; i < next.length; i++) {
    const a = next[i]!;
    a.vx += (cx - a.x) * 0.002;
    a.vy += (cy - a.y) * 0.002;
    for (let j = i + 1; j < next.length; j++) {
      const b = next[j]!;
      const dx = a.x - b.x;
      const dy = a.y - b.y;
      const dist = Math.max(40, Math.hypot(dx, dy));
      const force = 900 / (dist * dist);
      const fx = (dx / dist) * force;
      const fy = (dy / dist) * force;
      a.vx += fx;
      a.vy += fy;
      b.vx -= fx;
      b.vy -= fy;
    }
  }
  for (const edge of edges) {
    const a = byKey.get(keyOf(edge.from.kind, edge.from.id));
    const b = byKey.get(keyOf(edge.to.kind, edge.to.id));
    if (!a || !b) continue;
    const dx = b.x - a.x;
    const dy = b.y - a.y;
    const dist = Math.max(1, Math.hypot(dx, dy));
    const pull = (dist - 110) * 0.008;
    a.vx += (dx / dist) * pull;
    a.vy += (dy / dist) * pull;
    b.vx -= (dx / dist) * pull;
    b.vy -= (dy / dist) * pull;
  }
  for (const node of next) {
    node.vx *= 0.86;
    node.vy *= 0.86;
    node.x = clamp(node.x + node.vx, 24, width - 24);
    node.y = clamp(node.y + node.vy, 24, height - 28);
  }
  return next;
}

function clamp(value: number, min: number, max: number) {
  return Math.min(max, Math.max(min, value));
}
