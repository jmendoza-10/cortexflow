import { run, claudeCode } from "@ai-hero/sandcastle";
import { docker } from "@ai-hero/sandcastle/sandboxes/docker";
import { readFileSync, readdirSync } from "node:fs";
import { join } from "node:path";

// Pick up the next ready issue from cortexflow's local issue tracker
// (`.scratch/<feature>/issues/*.md`, see docs/agents/issue-tracker.md),
// run a single agent on it on its own branch, leave the branch for review.
//
// Cadence is human-driven: review the branch, merge if good, run this again
// to pick up the next ready issue. Flip a Status line in an issue file to
// `ready-for-agent` to queue it; the agent flips it to `ready-for-human`
// on success.

const ISSUES_DIR = ".scratch/cortexflow-v1/issues";

type Issue = { filename: string; slug: string; path: string; content: string };

const ready: Issue[] = readdirSync(ISSUES_DIR)
  .filter((f) => f.endsWith(".md"))
  .map((filename) => {
    const path = join(ISSUES_DIR, filename);
    return {
      filename,
      slug: filename.replace(/\.md$/, ""),
      path,
      content: readFileSync(path, "utf8"),
    };
  })
  .filter((i) => /^Status:\s*ready-for-agent\s*$/m.test(i.content))
  .sort((a, b) => a.filename.localeCompare(b.filename));

if (ready.length === 0) {
  console.log(`No issues with 'Status: ready-for-agent' in ${ISSUES_DIR}.`);
  console.log(`Flip a Status line and re-run.`);
  process.exit(0);
}

const issue = ready[0]!;
const branch = `agent/${issue.slug}`;

console.log(`Picked up: ${issue.filename}`);
console.log(`Branch:    ${branch}`);
console.log(`Queue:     ${ready.length - 1} more ready issue(s) for subsequent runs.\n`);

const prompt = `You are implementing a single cortexflow issue inside a sandboxed worktree on branch \`${branch}\`.

Project conventions live in CLAUDE.md at the repo root — read it first, then read any docs it references that touch this issue (CONTEXT.md, docs/adr/, docs/agents/issue-tracker.md, docs/agents/triage-labels.md, docs/prd.md).

The issue file is at \`${issue.path}\`. Its current contents are reproduced below for context, but treat the file on disk as the source of truth.

## What to do

1. Implement what the issue specifies, satisfying every acceptance-criteria checkbox.
2. Follow existing code conventions in the repo — look at neighboring files before introducing new patterns. Respect the architectural decisions in docs/adr/.
3. Commit your work with a clear message that references the issue file path.
4. When the implementation is complete, edit \`${issue.path}\`:
   - Change the \`Status:\` line to \`Status: ready-for-human\`.
   - Append (or add to) a \`## Comments\` section with: what you built, anything you skipped or deferred, anything the human reviewer should pay attention to. Sign the entry with the date and "from sandcastle agent".
   - Commit that edit as a separate commit so the diff is easy to read.
5. Output \`<promise>COMPLETE</promise>\` on its own line when done.

## When to bail out

If you hit a blocker you cannot resolve — missing context, ambiguous spec, required tooling absent from the container — stop. Do NOT flip the Status to ready-for-human. Instead:

- Append a \`## Comments\` entry explaining what you tried, what is missing, and what the human should decide.
- Commit that comment.
- Output \`<promise>COMPLETE</promise>\` (the work iteration is complete even though the issue isn't done).

The container has node, git, gh, jq, curl, and the Claude Code CLI. It does NOT currently have a C++ toolchain — if this issue requires compiling C++, write the code and tests but expect to bail out at the build step.

## Issue content

${issue.content}`;

await run({
  name: issue.slug,
  agent: claudeCode("claude-opus-4-6"),
  sandbox: docker(),
  branchStrategy: { type: "branch", branch },
  prompt,
  maxIterations: 5,
});

console.log(`\nRun complete. Review with:`);
console.log(`  git log ${branch} ^main`);
console.log(`  git diff main..${branch}`);
console.log(`  cat ${issue.path}    # check the Status line and Comments section`);
