// --- Game Gallery & Admin Management JS Logic ---
//
// 纯产品前端：游戏库（localStorage）、画廊卡片、管理后台表格、编辑弹窗。
// 只经 window.KrKr2Engine / KrKr2SpaceUI / KrKr2UI / KrKr2App 与引擎交互，
// 整个文件删除后引擎与本地文件选择器仍可用。
//
// 内联 onclick 依赖以下全局函数，文件末尾显式挂到 window 上：
//   launchRemoteGame / openGameEditModal / closeGameEditModal /
//   saveGameForm / deleteGame / closeAdminModal

(function () {
var STORAGE_KEY = 'krkr2-game-library';
var defaultGames = [
    {
        id: 'demo-game-1',
        name: '示例游戏：Fate / stay night (Demo)',
        cover: 'https://images.unsplash.com/photo-1578632767115-351597cf2477?w=500&q=80',
        url: 'https://raw.githubusercontent.com/fenghengzhi/krkr2/web/tests/assets/test_game.zip',
        entry: 'data.xp3',
        spaceId: 'space-fate-demo'
    }
];

function getGameLibrary() {
    try {
        var raw = localStorage.getItem(STORAGE_KEY);
        if (raw) {
            var parsed = JSON.parse(raw);
            if (Array.isArray(parsed) && parsed.length > 0) return parsed;
        }
    } catch (e) {
        console.error('Failed to read game library from localStorage:', e);
    }
    return defaultGames;
}

function saveGameLibrary(list) {
    try {
        localStorage.setItem(STORAGE_KEY, JSON.stringify(list));
    } catch (e) {
        console.error('Failed to save game library to localStorage:', e);
    }
}

function renderGameGallery() {
    var grid = document.getElementById('game-grid');
    var list = getGameLibrary();
    grid.innerHTML = '';

    if (list.length === 0) {
        grid.innerHTML =
            '<div class="empty-gallery">' +
            '<h3>游戏库中暂无游戏</h3>' +
            '<p>点击顶部“+ 新增游戏”添加远程资源，或“管理后台”导入预设配置</p>' +
            '<button class="btn-action primary" onclick="openGameEditModal()">+ 立即添加游戏</button>' +
            '</div>';
        return;
    }

    list.forEach(function(game) {
        var card = document.createElement('div');
        card.className = 'game-card';
        var isZip = (game.url || '').toLowerCase().indexOf('.zip') !== -1;
        var badgeText = isZip ? 'ZIP Package' : 'XP3 Archive';
        var coverHtml = game.cover ?
            '<img class="game-cover" src="' + escapeHtml(game.cover) + '" alt="' + escapeHtml(game.name) + '" onerror="this.onerror=null; this.parentElement.innerHTML=\'<div class=\\\'game-cover-fallback\\\'>&#127918;</div>\';">' :
            '<div class="game-cover-fallback">&#127918;</div>';

        card.innerHTML =
            '<div class="game-cover-container">' + coverHtml + '</div>' +
            '<div class="game-card-body">' +
            '   <div class="game-card-title" title="' + escapeHtml(game.name) + '">' + escapeHtml(game.name) + '</div>' +
            '   <div class="game-card-meta"><span class="game-badge">' + badgeText + '</span> ID: ' + escapeHtml(game.spaceId || game.id) + '</div>' +
            '   <div class="game-card-actions">' +
            '       <button class="btn-play" onclick="launchRemoteGame(\'' + escapeHtml(game.id) + '\')">&#9654; 启动游戏</button>' +
            '       <button class="btn-edit-card" title="编辑" onclick="openGameEditModal(\'' + escapeHtml(game.id) + '\')">&#9998;</button>' +
            '   </div>' +
            '</div>';
        grid.appendChild(card);
    });
}

function escapeHtml(str) {
    if (!str) return '';
    return String(str)
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;')
        .replace(/'/g, '&#039;');
}

async function launchRemoteGame(gameId) {
    var list = getGameLibrary();
    var game = list.find(function(g) { return g.id === gameId; });
    if (!game || !game.url) {
        alert('未查找到该游戏的链接信息');
        return;
    }

    var spaceId = game.spaceId || ('krkr2-' + game.id);
    await window.KrKr2Engine.setSaveSpace(spaceId);
    window.KrKr2SpaceUI.markSpaceChosen();

    document.getElementById('game-gallery-view').style.display = 'none';
    document.getElementById('gallery-header').style.display = 'none';
    window.KrKr2UI.hideFilePicker();

    if (game.url.toLowerCase().indexOf('.xp3') !== -1) {
        window.KrKr2App.startRemote({ type: 'xp3-url', url: game.url });
    } else {
        window.KrKr2App.startRemote({ type: 'zip-url', url: game.url, entry: game.entry });
    }
}

// --- Admin Modal Functions ---
function openAdminModal() {
    renderAdminTable();
    document.getElementById('admin-modal').classList.add('visible');
}

function closeAdminModal() {
    document.getElementById('admin-modal').classList.remove('visible');
}

function renderAdminTable() {
    var tbody = document.getElementById('admin-game-tbody');
    var list = getGameLibrary();
    tbody.innerHTML = '';

    if (list.length === 0) {
        tbody.innerHTML = '<tr><td colspan="5" style="text-align:center; color:#777; padding:20px;">暂无数据</td></tr>';
        return;
    }

    list.forEach(function(game) {
        var tr = document.createElement('tr');
        var coverImg = game.cover ?
            '<img class="admin-table-cover" src="' + escapeHtml(game.cover) + '" onerror="this.src=\'data:image/svg+xml;utf8,<svg xmlns=\\\'http://www.w3.org/2000/svg\\\' width=\\\'48\\\' height=\\\'48\\\'><rect width=\\\'48\\\' height=\\\'48\\\' fill=\\\'%230f3460\\\'/><text x=\\\'50%\\\' y=\\\'50%\\\' dominant-baseline=\\\'middle\\\' text-anchor=\\\'middle\\\' fill=\\\'%23e94560\\\' font-size=\\\'20\\\'>🎮</text></svg>\';">' :
            '<div class="admin-table-cover" style="display:flex;align-items:center;justify-content:center;">🎮</div>';

        var isZip = (game.url || '').toLowerCase().indexOf('.zip') !== -1;
        tr.innerHTML =
            '<td>' + coverImg + '</td>' +
            '<td><strong>' + escapeHtml(game.name) + '</strong><br><span style="font-size:11px;color:#777;">' + escapeHtml(game.url) + '</span></td>' +
            '<td>' + (isZip ? 'ZIP 包' : 'XP3 归档') + '</td>' +
            '<td><code>' + escapeHtml(game.spaceId || game.id) + '</code></td>' +
            '<td>' +
            '   <button class="btn-action" style="font-size:12px;padding:4px 8px;" onclick="openGameEditModal(\'' + escapeHtml(game.id) + '\')">编辑</button> ' +
            '   <button class="btn-action" style="font-size:12px;padding:4px 8px;border-color:#e94560;color:#e94560;" onclick="deleteGame(\'' + escapeHtml(game.id) + '\')">删除</button>' +
            '</td>';
        tbody.appendChild(tr);
    });
}

function openGameEditModal(gameId) {
    var form = document.getElementById('game-form');
    form.reset();
    if (gameId) {
        var list = getGameLibrary();
        var game = list.find(function(g) { return g.id === gameId; });
        if (game) {
            document.getElementById('edit-modal-title').textContent = '编辑游戏参数';
            document.getElementById('form-game-id').value = game.id;
            document.getElementById('form-game-name').value = game.name || '';
            document.getElementById('form-game-cover').value = game.cover || '';
            document.getElementById('form-game-url').value = game.url || '';
            document.getElementById('form-game-entry').value = game.entry || '';
            document.getElementById('form-game-space').value = game.spaceId || '';
        }
    } else {
        document.getElementById('edit-modal-title').textContent = '添加新游戏配置';
        document.getElementById('form-game-id').value = '';
        document.getElementById('form-game-space').value = 'space-' + Math.random().toString(36).substr(2, 6);
    }
    document.getElementById('game-edit-modal').classList.add('visible');
}

function closeGameEditModal() {
    document.getElementById('game-edit-modal').classList.remove('visible');
}

function saveGameForm(e) {
    e.preventDefault();
    var id = document.getElementById('form-game-id').value;
    var name = document.getElementById('form-game-name').value.trim();
    var cover = document.getElementById('form-game-cover').value.trim();
    var url = document.getElementById('form-game-url').value.trim();
    var entry = document.getElementById('form-game-entry').value.trim();
    var spaceId = document.getElementById('form-game-space').value.trim();

    if (!name || !url) {
        alert('请填写游戏名称和资源文件链接');
        return;
    }

    var list = getGameLibrary();
    if (id) {
        var idx = list.findIndex(function(g) { return g.id === id; });
        if (idx !== -1) {
            list[idx] = { id: id, name: name, cover: cover, url: url, entry: entry, spaceId: spaceId || id };
        }
    } else {
        var newId = 'game-' + Date.now();
        list.push({ id: newId, name: name, cover: cover, url: url, entry: entry, spaceId: spaceId || newId });
    }

    saveGameLibrary(list);
    closeGameEditModal();
    renderGameGallery();
    if (document.getElementById('admin-modal').classList.contains('visible')) {
        renderAdminTable();
    }
}

function deleteGame(gameId) {
    if (!confirm('确定要删除这个游戏配置吗？（游戏在 IndexedDB 中的存档不会受到影响）')) return;
    var list = getGameLibrary().filter(function(g) { return g.id !== gameId; });
    saveGameLibrary(list);
    renderGameGallery();
    renderAdminTable();
}

// Export and Import Config
document.getElementById('btn-export-json').addEventListener('click', function() {
    var dataStr = "data:text/json;charset=utf-8," + encodeURIComponent(JSON.stringify(getGameLibrary(), null, 2));
    var dlAnchor = document.createElement('a');
    dlAnchor.setAttribute("href", dataStr);
    dlAnchor.setAttribute("download", "krkr2-game-library.json");
    document.body.appendChild(dlAnchor);
    dlAnchor.click();
    dlAnchor.remove();
});

document.getElementById('btn-import-json').addEventListener('click', function() {
    document.getElementById('input-import-json').click();
});

document.getElementById('input-import-json').addEventListener('change', function(e) {
    var file = e.target.files[0];
    if (!file) return;
    var reader = new FileReader();
    reader.onload = function(evt) {
        try {
            var parsed = JSON.parse(evt.target.result);
            if (Array.isArray(parsed)) {
                saveGameLibrary(parsed);
                renderGameGallery();
                renderAdminTable();
                alert('成功导入 ' + parsed.length + ' 个游戏配置');
            } else {
                alert('导入失败：文件格式不符合 JSON 数组要求');
            }
        } catch(err) {
            alert('解析 JSON 失败：' + err.message);
        }
    };
    reader.readAsText(file);
    e.target.value = '';
});

// closeLocalPicker / openLocalPicker 已归属 js/ui/shell-ui.js（文件选择器
// 是引擎外壳而非画廊的一部分），这里直接复用它挂在 window 上的实现。

// Header Buttons
document.getElementById('btn-add-game').addEventListener('click', function() {
    openGameEditModal();
});
document.getElementById('btn-open-admin').addEventListener('click', function() {
    openAdminModal();
});
document.getElementById('btn-open-local').addEventListener('click', function() {
    window.openLocalPicker();
});

// Initial rendering on DOM Ready
document.addEventListener('DOMContentLoaded', function() {
    renderGameGallery();
});
// Render immediately in case DOMContentLoaded has already fired
renderGameGallery();

// 内联 onclick 依赖的全局导出
window.launchRemoteGame = launchRemoteGame;
window.openGameEditModal = openGameEditModal;
window.closeGameEditModal = closeGameEditModal;
window.saveGameForm = saveGameForm;
window.deleteGame = deleteGame;
window.closeAdminModal = closeAdminModal;

})();
