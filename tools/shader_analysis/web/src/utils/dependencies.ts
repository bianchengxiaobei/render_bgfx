import type { Instruction } from '../types';

export function registerKey(className: string, index: number): string {
  return `${className}:${index}`;
}

export function dependencyKeysFor(
  selectedKey: string | null,
  instructions: Instruction[],
  limit = 256,
): Set<string> {
  if (!selectedKey) {
    return new Set();
  }
  const edges = new Map<string, Set<string>>();
  for (const instruction of instructions) {
    if (instruction.reads.length === 0 || instruction.writes.length === 0) {
      continue;
    }
    for (const read of instruction.reads) {
      const from = registerKey(read.class, read.index);
      const targets = edges.get(from) ?? new Set<string>();
      for (const write of instruction.writes) {
        targets.add(registerKey(write.class, write.index));
      }
      edges.set(from, targets);
    }
  }

  const result = new Set<string>([selectedKey]);
  const queue = [selectedKey];
  while (queue.length > 0 && result.size < limit) {
    const key = queue.shift()!;
    for (const next of edges.get(key) ?? []) {
      if (!result.has(next)) {
        result.add(next);
        queue.push(next);
      }
    }
  }
  return result;
}
