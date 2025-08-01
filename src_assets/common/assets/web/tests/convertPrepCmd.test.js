import { describe, it, expect } from 'vitest';
import { convertPrepCmdsToEventActions } from '../utils/convertPrepCmd.js';

describe('convertPrepCmdsToEventActions', () => {
  it('converts prep commands to event actions with correct structure', () => {
    const prepCmds = [
      { do_cmd: 'echo setup', undo_cmd: 'echo cleanup', elevated: true },
      { do_cmd: 'echo setup2', undo_cmd: '', elevated: false },
    ];
    const result = convertPrepCmdsToEventActions(prepCmds);
    expect(Array.isArray(result)).toBe(true);
    expect(result.length).toBe(1);
    expect(result[0].name).toBe('Converted Prep Commands');
    expect(result[0].action.startup_stage).toBe('PRE_STREAM_START');
    expect(result[0].action.shutdown_stage).toBe('POST_STREAM_STOP');
    expect(result[0].action.startup_commands.commands.length).toBe(2);
    expect(result[0].action.startup_commands.commands[0].cmd).toBe('echo setup');
    expect(result[0].action.startup_commands.commands[0].elevated).toBe(true);
    expect(result[0].action.startup_commands.commands[1].cmd).toBe('echo setup2');
    expect(result[0].action.startup_commands.commands[1].elevated).toBe(false);
    expect(result[0].action.cleanup_commands.commands.length).toBe(1);
    expect(result[0].action.cleanup_commands.commands[0].cmd).toBe('echo cleanup');
    expect(result[0].action.cleanup_commands.commands[0].elevated).toBe(true);
  });

  it('returns empty array for empty input', () => {
    const result = convertPrepCmdsToEventActions([]);
    expect(result).toEqual([]);
  });

  it('handles missing elevated property', () => {
    const prepCmds = [
      { do_cmd: 'echo setup', undo_cmd: 'echo cleanup' },
    ];
    const result = convertPrepCmdsToEventActions(prepCmds);
    expect(result[0].action.startup_commands.commands[0].elevated).toBe(false);
    expect(result[0].action.cleanup_commands.commands[0].elevated).toBe(false);
  });
});
