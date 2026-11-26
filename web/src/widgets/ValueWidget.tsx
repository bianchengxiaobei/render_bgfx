import { useEffect, useMemo, useState } from 'react';
import type { CatalogSlot, JsonValue, SlotState } from '../api/types';
import {
  colorToHex,
  defaultGradientValue,
  defaultValueForType,
  effectiveSlotValue,
  formatValue,
  gradientCss,
  isDynamic,
  isGradientValue,
  isVectorType,
  parseColorHex,
  parseEditedValue,
  slotConcreteType,
} from '../graph/model';

interface ValueEditorProps {
  value: JsonValue | undefined;
  type: string;
  onCommit: (value: JsonValue) => void;
  compact?: boolean;
  disabled?: boolean;
}

export function ValueEditor({ value, type, onCommit, compact = false, disabled = false }: ValueEditorProps) {
  const normalized = type.toLowerCase();
  const effectiveValue = normalized === 'gradient' && !isGradientValue(value)
    ? defaultGradientValue()
    : value;
  const text = useMemo(() => formatValue(effectiveValue), [effectiveValue]);
  const [draft, setDraft] = useState(text);
  const [invalid, setInvalid] = useState(false);

  useEffect(() => {
    setDraft(text);
    setInvalid(false);
  }, [text]);

  if (normalized === 'gradient') {
    return (
      <div className={compact ? 'value-editor compact gradient' : 'value-editor gradient'}>
        <div className="gradient-preview" style={{ background: gradientCss(effectiveValue) }} />
        <textarea
          className="value-text gradient-text"
          value={draft}
          disabled={disabled}
          spellCheck={false}
          onChange={(event) => {
            setDraft(event.target.value);
            setInvalid(false);
          }}
          onBlur={() => {
            if (draft === text) {
              return;
            }
            const parsed = parseEditedValue(draft, normalized);
            if (parsed === null) {
              setInvalid(true);
              setDraft(text);
            } else {
              setInvalid(false);
              onCommit(parsed);
            }
          }}
        />
      </div>
    );
  }

  if (normalized === 'boolean') {
    return (
      <label className={compact ? 'value-editor compact boolean' : 'value-editor boolean'}>
        <input
          type="checkbox"
          checked={value === true}
          disabled={disabled}
          onChange={(event) => onCommit(event.target.checked)}
        />
        {!compact && <span>{value === true ? 'true' : 'false'}</span>}
      </label>
    );
  }

  if (normalized === 'color') {
    const alpha = Array.isArray(value) && typeof value[3] === 'number' ? value[3] : 1;
    return (
      <div className={compact ? 'value-editor compact color' : 'value-editor color'}>
        <input
          type="color"
          value={colorToHex(value)}
          disabled={disabled}
          onChange={(event) => setDraft(event.target.value)}
          onBlur={() => {
            const parsed = parseColorHex(draft, alpha);
            setDraft(formatValue(parsed));
            onCommit(parsed);
          }}
        />
        {!compact && (
          <input
            className="value-text"
            value={draft}
            disabled={disabled}
            onChange={(event) => setDraft(event.target.value)}
            onBlur={() => {
              const parsed = parseEditedValue(draft, normalized);
              if (parsed === null) {
                setInvalid(true);
                setDraft(text);
              } else {
                setInvalid(false);
                onCommit(parsed);
              }
            }}
            onKeyDown={(event) => {
              if (event.key === 'Enter') {
                event.currentTarget.blur();
              }
            }}
          />
        )}
      </div>
    );
  }

  return (
    <input
      className={[
        compact ? 'value-editor compact' : 'value-editor',
        invalid ? 'invalid' : '',
        isVectorType(normalized) ? 'vector' : '',
      ].filter(Boolean).join(' ')}
      value={draft}
      disabled={disabled}
      spellCheck={false}
      onChange={(event) => {
        setDraft(event.target.value);
        setInvalid(false);
      }}
      onBlur={() => {
        if (draft === text) {
          return;
        }
        const parsed = parseEditedValue(draft, normalized);
        if (parsed === null) {
          setInvalid(true);
          setDraft(text);
        } else {
          setInvalid(false);
          onCommit(parsed);
        }
      }}
      onKeyDown={(event) => {
        if (event.key === 'Enter') {
          event.currentTarget.blur();
        }
        if (event.key === 'Escape') {
          setDraft(text);
          event.currentTarget.blur();
        }
      }}
    />
  );
}

interface InlineSlotValueProps {
  nodeId: number;
  slot: CatalogSlot;
  state: SlotState | undefined;
  onCommit: (value: JsonValue) => void;
}

export function InlineSlotValue({ slot, state, onCommit }: InlineSlotValueProps) {
  const concrete = slotConcreteType(slot, state);
  const value = effectiveSlotValue(slot, state);
  const widgetType = isDynamic(slot.type) ? concrete : slot.type;
  return (
    <div className="inline-slot">
      <ValueEditor
        value={value}
        type={widgetType}
        compact
        onCommit={onCommit}
      />
    </div>
  );
}
