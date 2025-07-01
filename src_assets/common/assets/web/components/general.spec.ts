import useGeneral from './general';
import { computed } from 'vue';

describe('general component tests', () => {

    function makeConfig(prep: string | undefined, actions: string | undefined) {
        return {
            general: {
                global_prep_cmd: prep,
                global_event_actions: actions
            }
        };
    }

    const globalPrepCmd = [
        { "do": "powershell.exe -executionpolicy bypass -file \"D:\\sources\\AudioSwapper\\AudioSwapper.ps1\"", "elevated": false, "undo": "powershell.exe -executionpolicy bypass -file \"D:\\sources\\AudioSwapper\\AudioSwapper-Functions.ps1\" True" },
        { "do": "powershell.exe -executionpolicy bypass -file \"D:\\sources\\MonitorSwapAutomation\\StreamMonitor.ps1\" -n MonitorSwapper", "elevated": false, "undo": "powershell.exe -executionpolicy bypass -file \"D:\\sources\\MonitorSwapAutomation\\UndoScript.ps1\" -n MonitorSwapper" },
        { "do": "powershell.exe -executionpolicy bypass -file \"D:\\sources\\ResolutionAutomation\\StreamMonitor.ps1\" -n ResolutionMatcher", "elevated": false, "undo": "powershell.exe -executionpolicy bypass -file \"D:\\sources\\ResolutionAutomation\\UndoScript.ps1\" -n ResolutionMatcher" },
        { "do": "powershell.exe -executionpolicy bypass -file \"D:\\sources\\AutoHDRSwitch\\StreamMonitor.ps1\" -n AutoHDR", "elevated": false, "undo": "powershell.exe -executionpolicy bypass -file \"D:\\sources\\AutoHDRSwitch\\UndoScript.ps1\" -n AutoHDR" },
        { "do": "powershell.exe -executionpolicy bypass -file \"D:\\sources\\RTSSLimiter\\StreamMonitor.ps1\" -n RTSSLimiter", "elevated": true, "undo": "powershell.exe -executionpolicy bypass -file \"D:\\sources\\RTSSLimiter\\Helpers.ps1\" -n RTSSLimiter -t 1" },
        { "do": "", "elevated": false, "undo": "powershell.exe -executionpolicy bypass -windowstyle hidden -file \"D:\\sources\\PlayNite Watcher\\PlayNiteWatcher-EndScript.ps1\" True" }
    ];

    it('should convert pre commands in order including empty', () => {
        const expectedPre = {
            stage: 'PRE_STREAM_START',
            failure_policy: 'FAIL_FAST',
            commands: [
                { cmd: 'powershell.exe -executionpolicy bypass -file "D:\\sources\\AudioSwapper\\AudioSwapper.ps1"', admin: false, async: false, timeout: 30 },
                { cmd: 'powershell.exe -executionpolicy bypass -file "D:\\sources\\MonitorSwapAutomation\\StreamMonitor.ps1" -n MonitorSwapper', admin: false, async: false, timeout: 30 },
                { cmd: 'powershell.exe -executionpolicy bypass -file "D:\\sources\\ResolutionAutomation\\StreamMonitor.ps1" -n ResolutionMatcher', admin: false, async: false, timeout: 30 },
                { cmd: 'powershell.exe -executionpolicy bypass -file "D:\\sources\\AutoHDRSwitch\\StreamMonitor.ps1" -n AutoHDR', admin: false, async: false, timeout: 30 },
                { cmd: 'powershell.exe -executionpolicy bypass -file "D:\\sources\\RTSSLimiter\\StreamMonitor.ps1" -n RTSSLimiter', admin: true, async: false, timeout: 30 },
                { cmd: '', admin: false, async: false, timeout: 30 }
            ]
        };
        const { convertLegacyGlobalPrepCmd } = useGeneral({});
        const { pre } = convertLegacyGlobalPrepCmd(globalPrepCmd);
        expect(pre).toEqual(expectedPre);
    });

    it('should convert post commands in reverse order including empty', () => {
        const expectedPost = {
            stage: 'POST_STREAM_STOP',
            failure_policy: 'CONTINUE_ON_ERROR',
            commands: [
                { cmd: 'powershell.exe -executionpolicy bypass -windowstyle hidden -file "D:\\sources\\PlayNite Watcher\\PlayNiteWatcher-EndScript.ps1" True', admin: false, async: false, timeout: 30 },
                { cmd: 'powershell.exe -executionpolicy bypass -file "D:\\sources\\RTSSLimiter\\Helpers.ps1" -n RTSSLimiter -t 1', admin: true, async: false, timeout: 30 },
                { cmd: 'powershell.exe -executionpolicy bypass -file "D:\\sources\\AutoHDRSwitch\\UndoScript.ps1" -n AutoHDR', admin: false, async: false, timeout: 30 },
                { cmd: 'powershell.exe -executionpolicy bypass -file "D:\\sources\\ResolutionAutomation\\UndoScript.ps1" -n ResolutionMatcher', admin: false, async: false, timeout: 30 },
                { cmd: 'powershell.exe -executionpolicy bypass -file "D:\\sources\\MonitorSwapAutomation\\UndoScript.ps1" -n MonitorSwapper', admin: false, async: false, timeout: 30 },
                { cmd: 'powershell.exe -executionpolicy bypass -file "D:\\sources\\AudioSwapper\\AudioSwapper-Functions.ps1" True', admin: false, async: false, timeout: 30 }
            ]
        };
        const { convertLegacyGlobalPrepCmd } = useGeneral({});
        const { post } = convertLegacyGlobalPrepCmd(globalPrepCmd);
        expect(post).toEqual(expectedPost);
    });

    function makeConfigObj(prep: any, actions: any) {
        return {
            global_prep_cmd: JSON.stringify(prep),
            global_event_actions: JSON.stringify(actions)
        };
    }

    it('should suggest migration when prep is non-empty array and actions is missing', () => {
        const config = makeConfigObj([{ do: 'cmd', elevated: false, undo: 'undo' }], undefined);
        const { migrationSuggested } = useGeneral(config);
        expect(migrationSuggested.value).toBe(true);
    });

    it('should suggest migration when prep is non-empty array and actions is empty array', () => {
        const config = makeConfigObj([{ do: 'cmd', elevated: false, undo: 'undo' }], []);
        const { migrationSuggested } = useGeneral(config);
        expect(migrationSuggested.value).toBe(true);
    });

    it('should not suggest migration when prep is empty array', () => {
        const config = makeConfigObj([], []);
        const { migrationSuggested } = useGeneral(config);
        expect(migrationSuggested.value).toBe(false);
    });

    it('should not suggest migration when actions is non-empty array', () => {
        const config = makeConfigObj([{ do: 'cmd', elevated: false, undo: 'undo' }], [{ cmd: 'something' }]);
        const { migrationSuggested } = useGeneral(config);
        expect(migrationSuggested.value).toBe(false);
    });

    it('should not suggest migration when prep is not an array', () => {
        const config = makeConfigObj(undefined, []);
        const { migrationSuggested } = useGeneral(config);
        expect(migrationSuggested.value).toBe(false);
    });

    it('should not suggest migration when actions is not an array', () => {
        const config = makeConfigObj([{ do: 'cmd', elevated: false, undo: 'undo' }], undefined);
        const { migrationSuggested } = useGeneral(config);
        expect(migrationSuggested.value).toBe(true);
    });
});





