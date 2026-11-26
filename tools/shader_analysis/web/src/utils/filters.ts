import type { VariantSummary } from '../types';

function keywordName(define: string): string {
  const equals = define.indexOf('=');
  return (equals >= 0 ? define.slice(0, equals) : define).trim().toLowerCase();
}

export function matchesKeywordFilter(
  variant: VariantSummary,
  expression: string,
): boolean {
  const trimmed = expression.trim();
  if (!trimmed) {
    return true;
  }
  const groups = trimmed
    .split('|')
    .map((group) =>
      group
        .split('&')
        .map((keyword) => keyword.trim().toLowerCase())
        .filter(Boolean),
    )
    .filter((group) => group.length > 0);
  if (groups.length === 0) {
    return true;
  }
  const names = new Set(variant.canonicalDefines.map(keywordName));
  return groups.some((group) => group.every((keyword) => names.has(keyword)));
}
