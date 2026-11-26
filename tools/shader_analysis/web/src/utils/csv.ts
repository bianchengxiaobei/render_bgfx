import type { Metrics, ReportSummary, VariantSummary } from '../types';

const COLUMNS = [
  'Source',
  'Stage',
  'Backend',
  'VariantId',
  'Defines',
  'Status',
  'CompileMs',
  'BinaryBytes',
  'InstructionCount',
  'TempRegisterMax',
  'TempRegisterPeak',
  'SsaValueCount',
  'MaxLiveSsaValues',
  'TextureCount',
  'SamplerCount',
  'UavCount',
  'UniformBufferCount',
  'UniformBufferBytes',
  'StorageBufferCount',
  'CbufferDwordsRead',
  'DynamicCBufferReadCount',
  'WarningCount',
  'ErrorCount',
] as const;

function quote(value: string): string {
  return `"${value.replaceAll('"', '""')}"`;
}

function stringField(value: string): string {
  return `${quote(value)};`;
}

function intField(value: number | null | undefined): string {
  return `${value == null ? '' : String(value)};`;
}

function metric(
  variant: VariantSummary,
  pick: (metrics: Metrics) => number | null | undefined,
): number | null | undefined {
  return variant.metrics ? pick(variant.metrics) : null;
}

function appendVariant(row: string[], summary: ReportSummary, variant: VariantSummary) {
  row.push(stringField(summary.source.path));
  row.push(stringField(summary.stage));
  row.push(stringField(summary.backend));
  row.push(stringField(variant.variantId));
  row.push(stringField(variant.canonicalDefines.join('\r\n')));
  row.push(stringField(variant.status));
  row.push(intField(variant.compileMs));
  row.push(intField(variant.binaryBytes));
  row.push(intField(metric(variant, (m) => m.instructionCount)));
  row.push(intField(metric(variant, (m) => m.tempRegisterMax)));
  row.push(intField(metric(variant, (m) => m.tempRegisterPeak)));
  row.push(intField(metric(variant, (m) => m.ssaValueCount)));
  row.push(intField(metric(variant, (m) => m.maxLiveSsaValues)));
  row.push(intField(metric(variant, (m) => m.textureCount)));
  row.push(intField(metric(variant, (m) => m.samplerCount)));
  row.push(intField(metric(variant, (m) => m.uavCount)));
  row.push(intField(metric(variant, (m) => m.uniformBufferCount)));
  row.push(intField(metric(variant, (m) => m.uniformBufferBytes)));
  row.push(intField(metric(variant, (m) => m.storageBufferCount)));
  row.push(intField(metric(variant, (m) => m.cbufferDwordsRead)));
  row.push(intField(metric(variant, (m) => m.dynamicCBufferReadCount)));
  row.push(intField(metric(variant, (m) => m.warningCount)));
  row.push(intField(metric(variant, (m) => m.errorCount)));
}

export function summaryToCsv(summary: ReportSummary): string {
  const rows: string[] = [];
  rows.push(COLUMNS.map(stringField).join(''));
  const variants = [...summary.variants].sort((a, b) =>
    a.variantId.localeCompare(b.variantId),
  );
  for (const variant of variants) {
    const row: string[] = [];
    appendVariant(row, summary, variant);
    rows.push(row.join(''));
  }
  return `${rows.join('\r\n')}\r\n`;
}
