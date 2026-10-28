/**
 * Chinese reasoning.
 *
 * Registers one global system-prompt section: every Agent in this profile is
 * asked to write its internal reasoning / thinking content in Simplified
 * Chinese, while the final user-facing answer keeps following the user's
 * language. Code, identifiers, paths, commands, API names and quotes stay in
 * their original form.
 */
export const name = 'chinese-reasoning'

/** The prompt registry this row contributes to. */
export const inject = ['systemPrompt']

/** Unique section name; a duplicate registration would throw. */
const SECTION_NAME = 'local:chinese-reasoning'

/** Placed right after the deployment persona and before every tool policy. */
const SECTION_ORDER = 50

const SECTION_TEXT = [
  'Write your internal reasoning and thinking content in Simplified Chinese (简体中文).',
  'This applies to every reasoning block, even when the user writes in another language and even when most instructions here are in English.',
  'Keep code, identifiers, file paths, command lines, API names, URLs, and verbatim quotations exactly as they are; do not translate them.',
  'This rule governs reasoning only: the final answer follows the language of the user\'s request.',
  'Do not mention this language rule unless the user asks about it.',
].join(' ')

/**
 * Register the global reasoning-language section for the mounting scope.
 * @param ctx - Host plugin context.
 */
export function apply(ctx) {
  ctx.effect(
    () => ctx.systemPrompt.section({
      name: SECTION_NAME,
      order: SECTION_ORDER,
      text: SECTION_TEXT,
    }),
    'chinese-reasoning: global prompt section',
  )
}
