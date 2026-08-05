// --- Save space picker UI ---
//
// 存档空间 = 一个独立的 IndexedDB 数据库。非 FSA 路径下，用户必须先选定一个
// 空间（或明确跳过）才能加载游戏，故这里持有 spaceChosen 门与待执行动作。

(function () {
    var saveSpacePicker = document.getElementById('save-space-picker');
    var spaceStatus = document.getElementById('space-status');

    var spaceChosen = false;
    var pendingNonFsaAction = null;

    function formatSize(bytes) {
        if (bytes < 1024) return bytes + ' B';
        if (bytes < 1048576) return (bytes / 1024).toFixed(1) + ' KB';
        return (bytes / 1048576).toFixed(1) + ' MB';
    }

    async function refreshSpaceList() {
        var listEl = document.getElementById('space-list');
        listEl.innerHTML = '';
        spaceStatus.textContent = 'Loading...';
        var spaces = await window.KrKr2IDB.listSpaces();
        spaceStatus.textContent = '';
        if (spaces.length === 0) {
            listEl.innerHTML = '<div style="color:#666;font-size:13px;padding:12px;">No save spaces yet. Create one below.</div>';
            return;
        }
        for (var i = 0; i < spaces.length; i++) {
            (function (name) {
                var item = document.createElement('div');
                item.className = 'space-item';
                var info = document.createElement('div');
                info.className = 'space-info';
                info.innerHTML = '<div class="space-name">' + name + '</div><div class="space-meta">Loading...</div>';
                info.addEventListener('click', function () { selectSpace(name); });
                var exportBtn = document.createElement('button');
                exportBtn.textContent = 'Export';
                exportBtn.addEventListener('click', function () {
                    spaceStatus.textContent = 'Exporting...';
                    window.KrKr2IDB.exportZip(name).then(function () { spaceStatus.textContent = 'Exported!'; });
                });
                var deleteBtn = document.createElement('button');
                deleteBtn.className = 'delete-btn';
                deleteBtn.textContent = 'Delete';
                deleteBtn.addEventListener('click', function () {
                    if (confirm('Delete save space "' + name + '"? This cannot be undone.')) {
                        window.KrKr2IDB.deleteSpace(name).then(refreshSpaceList);
                    }
                });
                item.appendChild(info);
                item.appendChild(exportBtn);
                item.appendChild(deleteBtn);
                listEl.appendChild(item);
                window.KrKr2IDB.getSpaceInfo(name).then(function (si) {
                    info.querySelector('.space-meta').textContent = si.count + ' file(s), ' + formatSize(si.size);
                });
            })(spaces[i]);
        }
    }

    function runPendingAction() {
        if (pendingNonFsaAction) {
            var action = pendingNonFsaAction;
            pendingNonFsaAction = null;
            action();
        }
    }

    async function selectSpace(name) {
        await window.KrKr2Engine.setSaveSpace(name);
        spaceChosen = true;
        saveSpacePicker.classList.remove('visible');
        window.KrKr2UI.openFilePicker();
        runPendingAction();
    }

    function showSpacePicker() {
        refreshSpaceList();
        saveSpacePicker.classList.add('visible');
    }

    function ensureSpaceThen(action) {
        if (spaceChosen) { action(); return; }
        pendingNonFsaAction = action;
        window.KrKr2UI.hideFilePicker();
        showSpacePicker();
    }

    // 自动加载路径（?xp3= / ?game=）不弹选择器，直接沿用上次的空间。
    // FSA 可用时原实现刻意跳过：那条路径的存档直接回写用户选定的主机目录，
    // 不需要（也不应该）额外挂一个 IndexedDB 空间。
    var hasFileSystemAPI = typeof window.showDirectoryPicker === 'function';

    async function autoSelectSpace() {
        if (hasFileSystemAPI) return;
        var lastSpace = localStorage.getItem('krkr2-last-space');
        if (lastSpace) {
            // 复刻原 autoSelectSpace：只置位并打开，不改写 last-space、不登记
            await window.KrKr2Engine.setSaveSpace(lastSpace,
                { remember: false, register: false });
        }
    }

    function markSpaceChosen() { spaceChosen = true; }

    document.getElementById('space-create-btn').addEventListener('click', function () {
        var input = document.getElementById('space-name-input');
        var name = input.value.trim();
        if (!name) return;
        input.value = '';
        selectSpace(name);
    });

    document.getElementById('space-name-input').addEventListener('keydown', function (e) {
        if (e.key === 'Enter') document.getElementById('space-create-btn').click();
    });

    document.getElementById('space-skip-btn').addEventListener('click', function () {
        window.KrKr2Engine.setSaveSpace(null);
        spaceChosen = true;
        saveSpacePicker.classList.remove('visible');
        window.KrKr2UI.openFilePicker();
        runPendingAction();
    });

    document.getElementById('space-import-btn').addEventListener('click', function () {
        document.getElementById('space-import-input').click();
    });

    document.getElementById('space-import-input').addEventListener('change', function (e) {
        var file = e.target.files[0];
        if (!file) return;
        e.target.value = '';
        spaceStatus.textContent = 'Importing...';
        window.KrKr2IDB.importZip(file).then(function (name) {
            spaceStatus.textContent = 'Imported "' + name + '"';
            refreshSpaceList();
        }).catch(function (err) {
            spaceStatus.textContent = 'Import failed: ' + err.message;
        });
    });

    window.KrKr2SpaceUI = {
        selectSpace: selectSpace,
        showSpacePicker: showSpacePicker,
        ensureSpaceThen: ensureSpaceThen,
        autoSelectSpace: autoSelectSpace,
        markSpaceChosen: markSpaceChosen,
        refreshSpaceList: refreshSpaceList
    };
})();
