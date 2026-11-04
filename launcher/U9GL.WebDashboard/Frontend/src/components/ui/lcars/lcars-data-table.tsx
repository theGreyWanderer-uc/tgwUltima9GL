"use client";

import { useId, useState, type ReactNode } from "react";
import { LcarsButton } from "./lcars-button";
import { LcarsInput } from "./lcars-input";
import {
  compareLcarsNumericValues,
  formatLcarsReading,
  isLcarsNumericValue,
  type LcarsNumericValue,
} from "./lcars-numeric";
import "./lcars.css";

export type LcarsDataColumn<Row> = {
  id: string;
  header: string;
  value: (row: Row) => LcarsNumericValue | null;
  numeric?: boolean;
  unit?: string;
  precision?: number;
  sortable?: boolean;
  render?: (row: Row) => ReactNode;
};
export type LcarsTableData<Row> =
  | { state: "ready"; rows: readonly Row[] }
  | { state: "loading" }
  | { state: "error"; message: string };
export type LcarsTableSelection = {
  selectedIds: readonly string[];
  onChange: (ids: readonly string[]) => void;
};
export type LcarsDataTableProps<Row> = {
  label: string;
  columns: readonly LcarsDataColumn<Row>[];
  data: LcarsTableData<Row>;
  getRowId: (row: Row) => string;
  getRowLabel?: (row: Row) => string;
  pageSize?: number;
  selection?: LcarsTableSelection;
};

export function LcarsDataTable<Row>({
  label,
  columns,
  data,
  getRowId,
  getRowLabel = getRowId,
  pageSize = 5,
  selection,
}: LcarsDataTableProps<Row>) {
  const scope = useId();
  const [query, setQuery] = useState("");
  const [page, setPage] = useState(0);
  const [sort, setSort] = useState<{
    id: string;
    direction: "ascending" | "descending";
  } | null>(null);
  if (
    !Number.isInteger(pageSize) ||
    pageSize < 1 ||
    pageSize > 100 ||
    columns.length === 0 ||
    new Set(columns.map((column) => column.id)).size !== columns.length ||
    columns.some((column) => !column.id)
  )
    throw new Error(
      "LCARS data table requires unique nonempty columns and a page size from 1 to 100.",
    );
  const rows = data.state === "ready" ? data.rows : [];
  const ids = rows.map(getRowId);
  if (new Set(ids).size !== ids.length || ids.some((id) => !id))
    throw new Error("LCARS data table requires unique nonempty row ids.");
  const display = (column: LcarsDataColumn<Row>, row: Row) => {
    const value = column.value(row);
    return column.numeric
      ? formatLcarsReading(value, column.unit, column.precision ?? 20)
      : value === null || (typeof value === "number" && !Number.isFinite(value))
        ? "Unavailable"
        : String(value);
  };
  const filtered = rows
    .map((row, index) => ({ row, index }))
    .filter(
      ({ row }) =>
        !query.trim() ||
        columns.some((column) =>
          `${column.value(row) ?? ""} ${display(column, row)}`
            .toLocaleLowerCase("en-US")
            .includes(query.trim().toLocaleLowerCase("en-US")),
        ),
    );
  const column = columns.find(
    (column) => column.id === sort?.id && column.sortable !== false,
  );
  if (sort && column)
    filtered.sort((a, b) => {
      const left = column.value(a.row),
        right = column.value(b.row);
      const missingLeft = column.numeric
        ? !isLcarsNumericValue(left)
        : left === null;
      const missingRight = column.numeric
        ? !isLcarsNumericValue(right)
        : right === null;
      if (missingLeft || missingRight)
        return missingLeft === missingRight
          ? a.index - b.index
          : missingLeft
            ? 1
            : -1;
      const result = column.numeric
        ? compareLcarsNumericValues(left, right)
        : String(left).localeCompare(String(right), "en-US", {
            sensitivity: "base",
          });
      return (
        result * (sort.direction === "ascending" ? 1 : -1) || a.index - b.index
      );
    });
  const pages = Math.max(1, Math.ceil(filtered.length / pageSize));
  const current = Math.min(page, pages - 1);
  if (data.state === "ready" && current !== page) setPage(current);
  const visible = filtered.slice(current * pageSize, (current + 1) * pageSize);
  const selected =
    selection?.selectedIds.filter((id) => ids.includes(id)) ?? [];
  const allVisible =
    visible.length > 0 &&
    visible.every(({ row }) => selected.includes(getRowId(row)));
  const someVisible = visible.some(({ row }) =>
    selected.includes(getRowId(row)),
  );
  return (
    <section
      className="lcars-data-table"
      aria-label={label}
      aria-busy={data.state === "loading"}
    >
      <LcarsInput
        id={`${scope}-filter`}
        type="search"
        label={`Filter ${label}`}
        value={query}
        disabled={data.state !== "ready"}
        onChange={(event) => {
          setQuery(event.target.value);
          setPage(0);
        }}
      />
      <div
        className="lcars-table-wrap"
        tabIndex={0}
        role="region"
        aria-label={`${label} table scroll area`}
      >
        <table className="lcars-table">
          <caption>{label}</caption>
          <thead>
            <tr>
              {selection && (
                <th scope="col">
                  <label className="lcars-table-check">
                    <input
                      type="checkbox"
                      aria-label={`Select current page of ${label}`}
                      checked={allVisible}
                      disabled={!visible.length}
                      ref={(input) => {
                        if (input)
                          input.indeterminate = someVisible && !allVisible;
                      }}
                      onChange={() => {
                        const pageIds = visible.map(({ row }) => getRowId(row));
                        selection.onChange(
                          allVisible
                            ? selected.filter((id) => !pageIds.includes(id))
                            : [...new Set([...selected, ...pageIds])],
                        );
                      }}
                    />
                  </label>
                </th>
              )}
              {columns.map((column) => (
                <th
                  key={column.id}
                  scope="col"
                  data-numeric={column.numeric}
                  aria-sort={
                    column.sortable === false
                      ? undefined
                      : sort?.id === column.id
                        ? sort.direction
                        : "none"
                  }
                >
                  {column.sortable === false ? (
                    column.header
                  ) : (
                    <button
                      className="lcars-table-sort"
                      type="button"
                      disabled={data.state !== "ready" || rows.length === 0}
                      onClick={() => {
                        setSort(
                          sort?.id !== column.id
                            ? { id: column.id, direction: "ascending" }
                            : sort.direction === "ascending"
                              ? { id: column.id, direction: "descending" }
                              : null,
                        );
                        setPage(0);
                      }}
                    >
                      {column.header}
                      <span aria-hidden="true">
                        {sort?.id === column.id
                          ? sort.direction === "ascending"
                            ? " ↑"
                            : " ↓"
                          : " ↕"}
                      </span>
                    </button>
                  )}
                </th>
              ))}
            </tr>
          </thead>
          <tbody>
            {visible.map(({ row }) => {
              const id = getRowId(row);
              return (
                <tr key={id} data-selected={selected.includes(id)}>
                  {selection && (
                    <td>
                      <label className="lcars-table-check">
                        <input
                          type="checkbox"
                          aria-label={`Select ${getRowLabel(row)}`}
                          checked={selected.includes(id)}
                          onChange={() =>
                            selection.onChange(
                              selected.includes(id)
                                ? selected.filter((entry) => entry !== id)
                                : [...selected, id],
                            )
                          }
                        />
                      </label>
                    </td>
                  )}
                  {columns.map((column, index) => {
                    const Tag = index === 0 ? "th" : "td";
                    return (
                      <Tag
                        key={column.id}
                        scope={index === 0 ? "row" : undefined}
                        data-numeric={column.numeric}
                      >
                        {column.render
                          ? column.render(row)
                          : display(column, row)}
                      </Tag>
                    );
                  })}
                </tr>
              );
            })}
            {visible.length === 0 && (
              <tr>
                <td colSpan={columns.length + (selection ? 1 : 0)}>
                  {data.state === "loading"
                    ? "Loading records…"
                    : data.state === "error"
                      ? "Records unavailable."
                      : rows.length === 0
                        ? "No records."
                        : "No matching records."}
                </td>
              </tr>
            )}
          </tbody>
        </table>
      </div>
      {data.state === "error" && (
        <p className="lcars-field-error" role="alert">
          {data.message}
        </p>
      )}
      <nav
        className="lcars-table-pagination"
        aria-label={`${label} pagination`}
      >
        <LcarsButton
          tone="secondary"
          disabled={current === 0 || data.state !== "ready"}
          onClick={() => setPage(current - 1)}
        >
          Previous page
        </LcarsButton>
        <p>
          Page {current + 1} of {pages} · {filtered.length} of {rows.length}{" "}
          records{selection ? ` · ${selected.length} selected` : ""}
        </p>
        <LcarsButton
          tone="secondary"
          disabled={current + 1 >= pages || data.state !== "ready"}
          onClick={() => setPage(current + 1)}
        >
          Next page
        </LcarsButton>
      </nav>
    </section>
  );
}
