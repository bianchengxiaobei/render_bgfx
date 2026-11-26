import { describe, expect, it } from 'vitest';
import type { CatalogSlot, Graph } from '../src/api/types';
import {
  colorToHex,
  connectionCompatible,
  defaultGradientValue,
  gradientCss,
  parseEditedValue,
  slotConcreteType,
  withFallbackLayout,
} from '../src/graph/model';

function slot(overrides: Partial<CatalogSlot>): CatalogSlot {
  return {
    id: 0,
    name: 'Slot',
    displayName: 'Slot',
    direction: 'in',
    type: 'float',
    required: false,
    hidden: false,
    allowConnection: true,
    supportsLiteral: true,
    requiresLiteral: false,
    canPropagateLiteral: false,
    allowedConcreteTypes: [],
    ...overrides,
  };
}

function graphWithZeroPositions(): Graph {
  return {
    schemaVersion: 1,
    kind: 'graph',
    name: 'Fallback',
    target: { id: 'lit', subTarget: 'lit', settings: {} },
    properties: [],
    keywords: [],
    categories: [],
    nodes: [
      { id: 1, type: 'UV', typeVersion: 1, position: [0, 0], settings: {}, slots: {} },
      { id: 2, type: 'Sample Texture 2D', typeVersion: 1, position: [0, 0], settings: {}, slots: {} },
      { id: 3, type: 'Multiply', typeVersion: 1, position: [0, 0], settings: {}, slots: {} },
    ],
    edges: [
      { from: { node: 1, slot: 0 }, to: { node: 2, slot: 0 } },
      { from: { node: 2, slot: 2 }, to: { node: 3, slot: 0 } },
    ],
    groups: [],
    notes: [],
  };
}

describe('graph model', () => {
  it('assigns deterministic columns when a fixture has no layout positions', () => {
    const source = graphWithZeroPositions();
    const laidOut = withFallbackLayout(source);
    expect(source.nodes[1].position).toEqual([0, 0]);
    expect(laidOut.nodes.map((node) => node.position)).toEqual([
      [80, 80],
      [370, 80],
      [660, 80],
    ]);
  });

  it('keeps explicit positions untouched', () => {
    const source = graphWithZeroPositions();
    source.nodes[0].position = [10, 20];
    expect(withFallbackLayout(source).nodes[0].position).toEqual([10, 20]);
  });

  it('uses float as the default dynamic-vector concrete type', () => {
    expect(slotConcreteType(slot({ type: 'dynamicvector' }))).toBe('float');
    expect(slotConcreteType(slot({ type: 'dynamicvector' }), { mode: 'default', concreteType: 'vector3' }))
      .toBe('vector3');
  });

  it('validates output-to-input type compatibility locally', () => {
    expect(connectionCompatible(
      slot({ direction: 'out', type: 'vector4' }),
      slot({ direction: 'in', type: 'color' }),
    )).toBe(true);
    expect(connectionCompatible(
      slot({ direction: 'out', type: 'texture2d' }),
      slot({ direction: 'in', type: 'float' }),
    )).toBe(false);
    expect(connectionCompatible(
      slot({ direction: 'out', type: 'float' }),
      slot({ direction: 'in', type: 'vector4', allowedConcreteTypes: ['vector2'] }),
    )).toBe(false);
    expect(connectionCompatible(
      slot({ direction: 'out', type: 'dynamicvector' }),
      slot({ direction: 'in', type: 'color' }),
    )).toBe(true);
  });

  it('parses scalar, vector, and color edits', () => {
    expect(parseEditedValue('1.5', 'float')).toBe(1.5);
    expect(parseEditedValue('1, 2, 3', 'vector3')).toEqual([1, 2, 3]);
    expect(parseEditedValue('nope', 'float')).toBeNull();
    expect(colorToHex([1, 0.5, 0, 1])).toBe('#ff8000');
    expect(parseEditedValue('#ff8000', 'color')).toEqual([1, 0.5019607843137255, 0, 1]);
  });

  it('parses gradient settings and builds a preview gradient', () => {
    const gradient = defaultGradientValue();
    expect(parseEditedValue(JSON.stringify(gradient), 'gradient')).toEqual(gradient);
    expect(parseEditedValue('{"keys":[]}', 'gradient')).toEqual({ keys: [] });
    expect(parseEditedValue('not json', 'gradient')).toBeNull();
    expect(parseEditedValue('{"keys":[{"t":0}]}', 'gradient')).toBeNull();
    expect(gradientCss(gradient)).toContain('linear-gradient(90deg');
  });
});
