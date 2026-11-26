import type { Instruction } from '../types';

function refsText(refs: Instruction['reads']): string {
  return refs.map((ref) => `${ref.class}${ref.index}${ref.mask}`).join(', ');
}

function instructionText(instruction: Instruction): string {
  return [
    instruction.text,
    instruction.opcode,
    refsText(instruction.reads),
    refsText(instruction.writes),
  ]
    .join(' ')
    .toLowerCase();
}

export function matchingInstructionIndices(
  instructions: Instruction[],
  query: string,
): number[] {
  const needle = query.trim().toLowerCase();
  if (!needle) {
    return [];
  }
  return instructions
    .filter((instruction) => instructionText(instruction).includes(needle))
    .map((instruction) => instruction.index);
}

export function clampMatchPosition(position: number | null, count: number): number | null {
  if (count <= 0) {
    return null;
  }
  if (position == null || !Number.isFinite(position)) {
    return 0;
  }
  return Math.max(0, Math.min(count - 1, Math.floor(position)));
}
