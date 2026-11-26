import { useCallback, useEffect, useMemo, useRef } from 'react';
import {
  Background,
  Controls,
  MarkerType,
  MiniMap,
  ReactFlow,
  useReactFlow,
  type Connection,
  type Edge,
  type Node,
  type NodeChange,
  type NodeTypes,
  type OnNodeDrag,
} from '@xyflow/react';
import type { Command } from '../api/types';
import { useEditorStore } from '../state/editor';
import { GraphNodeView, type GraphNodeData } from './GraphNode';
import { connectionCompatible, edgeId, findNode, slotConcreteType, slotSpec, typeColor } from './model';

const nodeTypes: NodeTypes = { sgNode: GraphNodeView };

interface DragStart {
  positions: Map<number, { x: number; y: number }>;
}

export function GraphCanvas() {
  const graph = useEditorStore((state) => state.graph);
  const documentPath = useEditorStore((state) => state.document?.path ?? '');
  const catalogMap = useEditorStore((state) => state.catalogMap);
  const revision = useEditorStore((state) => state.revision);
  const diagnostics = useEditorStore((state) => state.diagnostics);
  const selection = useEditorStore((state) => state.selection);
  const showPreviews = useEditorStore((state) => state.showPreviews);
  const focusNodeId = useEditorStore((state) => state.focusNodeId);
  const setSelection = useEditorStore((state) => state.setSelection);
  const currentSelection = useEditorStore((state) => state.selection);
  const setLocalNodePositions = useEditorStore((state) => state.setLocalNodePositions);
  const commitMoves = useEditorStore((state) => state.commitMoves);
  const connect = useEditorStore((state) => state.connect);
  const runCommands = useEditorStore((state) => state.runCommands);
  const addNode = useEditorStore((state) => state.addNode);
  const notify = useEditorStore((state) => state.notify);
  const openSearch = useEditorStore((state) => state.openSearch);
  const setFocusNode = useEditorStore((state) => state.setFocusNode);
  const { screenToFlowPosition, fitView, getNode, setCenter } = useReactFlow();

  const dragStart = useRef<DragStart>({ positions: new Map() });
  const lastMouse = useRef({ x: 0, y: 0 });
  const fittedPath = useRef('');

  const nodes = useMemo<Node<GraphNodeData>[]>(() => {
    if (!graph) {
      return [];
    }
    const selected = new Set(selection.nodes);
    return graph.nodes.map((node) => ({
      id: String(node.id),
      type: 'sgNode',
      position: { x: node.position[0], y: node.position[1] },
      data: {
        node,
        definition: catalogMap[node.type],
        diagnostics: diagnostics.filter((diagnostic) => diagnostic.nodeId === node.id),
        revision,
        showPreviews,
      },
      selected: selected.has(node.id),
    }));
  }, [catalogMap, diagnostics, graph, revision, selection.nodes, showPreviews]);

  const edges = useMemo<Edge[]>(() => {
    if (!graph) {
      return [];
    }
    return graph.edges.map((edge) => {
      const sourceNode = findNode(graph, edge.from.node);
      const source = sourceNode ? slotSpec(catalogMap, sourceNode, edge.from.slot) : undefined;
      const state = sourceNode?.slots[String(edge.from.slot)];
      const color = source ? typeColor(slotConcreteType(source, state)) : '#8e8e8e';
      return {
        id: edgeId(edge),
        source: String(edge.from.node),
        target: String(edge.to.node),
        sourceHandle: String(edge.from.slot),
        targetHandle: String(edge.to.slot),
        type: 'smoothstep',
        animated: false,
        style: { stroke: color, strokeWidth: 1.6 },
        markerEnd: { type: MarkerType.ArrowClosed, color, width: 11, height: 11 },
        selected: selection.edges.includes(edgeId(edge)),
      };
    });
  }, [catalogMap, graph, selection.edges]);

  useEffect(() => {
    if (!graph || !documentPath || fittedPath.current === documentPath) {
      return;
    }
    fittedPath.current = documentPath;
    const timer = window.setTimeout(() => {
      void fitView({ padding: 0.18, duration: 180, maxZoom: 1 });
    }, 30);
    return () => window.clearTimeout(timer);
  }, [documentPath, fitView, graph]);

  useEffect(() => {
    if (focusNodeId === null) {
      return;
    }
    const node = graph?.nodes.find((item) => item.id === focusNodeId);
    const flowNode = getNode(String(focusNodeId));
    if (node && flowNode) {
      const width = flowNode.measured?.width ?? 220;
      const height = flowNode.measured?.height ?? 140;
      void setCenter(node.position[0] + width / 2, node.position[1] + height / 2, {
        zoom: 0.95,
        duration: 240,
      });
    }
    setSelection({ nodes: [focusNodeId], edges: [] });
    setFocusNode(null);
  }, [focusNodeId, getNode, graph, setCenter, setFocusNode, setSelection]);

  useEffect(() => {
    const onKeyDown = (event: KeyboardEvent) => {
      const target = event.target as HTMLElement | null;
      if (target && (target.tagName === 'INPUT' || target.tagName === 'TEXTAREA' ||
          target.tagName === 'SELECT' || target.isContentEditable)) {
        return;
      }
      if (event.code === 'Space' && !event.ctrlKey && !event.metaKey && !event.altKey) {
        event.preventDefault();
        openSearch(screenToFlowPosition(lastMouse.current));
      }
    };
    window.addEventListener('keydown', onKeyDown);
    return () => window.removeEventListener('keydown', onKeyDown);
  }, [openSearch, screenToFlowPosition]);

  const onNodesChange = useCallback((changes: NodeChange[]) => {
    const positions: Record<number, { x: number; y: number }> = {};
    const selected = new Set(currentSelection.nodes);
    let selectionChanged = false;
    for (const change of changes) {
      if (change.type === 'position' && change.position) {
        positions[Number(change.id)] = { x: change.position.x, y: change.position.y };
      } else if (change.type === 'select') {
        const id = Number(change.id);
        if (change.selected) {
          selected.add(id);
        } else {
          selected.delete(id);
        }
        selectionChanged = true;
      }
    }
    if (Object.keys(positions).length > 0) {
      setLocalNodePositions(positions);
    }
    if (selectionChanged) {
      setSelection({ nodes: Array.from(selected), edges: currentSelection.edges });
    }
  }, [currentSelection.edges, currentSelection.nodes, setLocalNodePositions, setSelection]);

  const onNodeDragStart: OnNodeDrag = useCallback((_event, node, dragged) => {
    const positions = new Map<number, { x: number; y: number }>();
    for (const item of dragged.length > 0 ? dragged : [node]) {
      positions.set(Number(item.id), { x: item.position.x, y: item.position.y });
    }
    dragStart.current = { positions };
  }, []);

  const onNodeDragStop: OnNodeDrag = useCallback((_event, node, dragged) => {
    const moved = dragged.length > 0 ? dragged : [node];
    const first = moved[0];
    const start = dragStart.current.positions.get(Number(first.id));
    if (!start) {
      return;
    }
    const delta = { x: first.position.x - start.x, y: first.position.y - start.y };
    if (Math.abs(delta.x) < 0.1 && Math.abs(delta.y) < 0.1) {
      return;
    }
    void commitMoves(moved.map((item) => Number(item.id)), delta);
  }, [commitMoves]);

  const onConnect = useCallback((connection: Connection) => {
    if (!graph || !connection.source || !connection.target ||
        connection.sourceHandle === null || connection.targetHandle === null) {
      return;
    }
    const sourceNode = findNode(graph, Number(connection.source));
    const targetNode = findNode(graph, Number(connection.target));
    if (!sourceNode || !targetNode) {
      return;
    }
    const sourceSlot = slotSpec(catalogMap, sourceNode, Number(connection.sourceHandle));
    const targetSlot = slotSpec(catalogMap, targetNode, Number(connection.targetHandle));
    if (!sourceSlot || !targetSlot) {
      notify('error', '无法识别端口，连接已取消。');
      return;
    }
    const sourceState = sourceNode.slots[String(sourceSlot.id)];
    const targetState = targetNode.slots[String(targetSlot.id)];
    if (!connectionCompatible(sourceSlot, targetSlot, sourceState, targetState)) {
      notify('error', `类型不兼容：${sourceSlot.displayName} → ${targetSlot.displayName}`);
      return;
    }
    void connect(
      { node: Number(connection.source), slot: Number(connection.sourceHandle) },
      { node: Number(connection.target), slot: Number(connection.targetHandle) },
    );
  }, [catalogMap, connect, graph, notify]);

  const onDelete = useCallback(({ nodes: deletedNodes, edges: deletedEdges }: {
    nodes: Node[];
    edges: Edge[];
  }) => {
    if (!graph) {
      return;
    }
    const deletedIds = new Set(deletedNodes.map((node) => Number(node.id)));
    const commands: Command[] = [];
    if (deletedIds.size > 0) {
      commands.push({ op: 'removeNodes', ids: Array.from(deletedIds) });
    }
    for (const deletedEdge of deletedEdges) {
      const edge = graph.edges.find((item) => edgeId(item) === deletedEdge.id);
      if (edge && !deletedIds.has(edge.from.node) && !deletedIds.has(edge.to.node)) {
        commands.push({
          op: 'disconnect',
          from: { node: edge.from.node, slot: edge.from.slot },
          to: { node: edge.to.node, slot: edge.to.slot },
        });
      }
    }
    if (commands.length > 0) {
      void runCommands(commands, { refresh: true });
    }
  }, [graph, runCommands]);

  const onDrop = useCallback((event: React.DragEvent) => {
    event.preventDefault();
    const property = event.dataTransfer.getData('application/x-sg-property');
    const keyword = event.dataTransfer.getData('application/x-sg-keyword');
    if (!property && !keyword) {
      return;
    }
    const position = screenToFlowPosition({ x: event.clientX, y: event.clientY });
    if (property) {
      void addNode('Property', position, { property });
    } else {
      void addNode('Keyword', position, { keyword });
    }
  }, [addNode, screenToFlowPosition]);

  return (
    <div
      className="graph-canvas"
      onDrop={onDrop}
      onDragOver={(event) => {
        event.preventDefault();
        event.dataTransfer.dropEffect = 'copy';
      }}
      onMouseMove={(event) => {
        lastMouse.current = { x: event.clientX, y: event.clientY };
      }}
    >
      <ReactFlow
        nodes={nodes}
        edges={edges}
        nodeTypes={nodeTypes}
        onNodesChange={onNodesChange}
        onEdgesChange={(changes) => {
          const selectedEdges = new Set(currentSelection.edges);
          let changed = false;
          for (const change of changes) {
            if (change.type === 'select') {
              if (change.selected) {
                selectedEdges.add(change.id);
              } else {
                selectedEdges.delete(change.id);
              }
              changed = true;
            }
          }
          if (changed) {
            setSelection({ nodes: currentSelection.nodes, edges: Array.from(selectedEdges) });
          }
        }}
        onPaneClick={() => setSelection({ nodes: [], edges: [] })}
        onNodeDragStart={onNodeDragStart}
        onNodeDragStop={onNodeDragStop}
        onConnect={onConnect}
        onDelete={onDelete}
        isValidConnection={(connection) =>
          Boolean(connection.source && connection.target &&
            connection.sourceHandle !== null && connection.targetHandle !== null)
        }
        deleteKeyCode={['Delete']}
        selectionOnDrag
        panOnScroll
        minZoom={0.15}
        maxZoom={2.5}
        defaultEdgeOptions={{ type: 'smoothstep' }}
        connectionLineStyle={{ stroke: '#b8b8b8', strokeWidth: 1.5 }}
        fitView
      >
        <Background color="#343434" gap={18} size={1} />
        <Controls showInteractive={false} />
        <MiniMap
          pannable
          zoomable
          nodeColor={(node) => {
            const data = node.data as unknown as GraphNodeData;
            return data.definition?.blockNode ? '#8f5a3a' : '#46515e';
          }}
          maskColor="rgba(15, 15, 15, 0.55)"
        />
      </ReactFlow>
      {!graph && <div className="canvas-empty">没有打开的文档</div>}
    </div>
  );
}
