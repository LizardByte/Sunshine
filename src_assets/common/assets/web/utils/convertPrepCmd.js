// Utility to convert legacy prep commands to event actions format
// Used for both global and per-app conversion

/**
 * Convert legacy prep commands to event actions format
 * @param {Array<{do_cmd: string, undo_cmd: string, elevated: boolean}>} prepCmds
 * @returns {Array} array of action objects in new format
 */
export function convertPrepCmdsToEventActions(prepCmds) {
  if (!Array.isArray(prepCmds) || prepCmds.length === 0) return [];

  const action = {
    name: 'Converted Prep Commands',
    action: {
      startup_stage: 'PRE_STREAM_START',
      shutdown_stage: 'POST_STREAM_STOP',
      startup_commands: {
        failure_policy: 'FAIL_FAST',
        commands: prepCmds.filter(c => c.do_cmd).map(c => ({
          cmd: c.do_cmd,
          elevated: !!c.elevated,
          timeout_seconds: 30,
          ignore_error: false,
          async: false
        }))
      },
      cleanup_commands: {
        failure_policy: 'CONTINUE_ON_FAILURE',
        commands: prepCmds.slice().reverse().filter(c => c.undo_cmd).map(c => ({
          cmd: c.undo_cmd,
          elevated: !!c.elevated,
          timeout_seconds: 30,
          ignore_error: true,
          async: false
        }))
      }
    }
  };

  return [action];
}
