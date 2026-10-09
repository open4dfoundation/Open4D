'use strict';

const fs = require('fs');
const path = require('path');

function validDirectoryId(id, maxLength = 180) {
    return typeof id === 'string' && id.length <= maxLength
        && /^[A-Za-z0-9._-]+$/.test(id) && id !== '.' && id !== '..';
}

/** Resolve one directory component, rejecting traversal and existing symlinks. */
function childDirectory(root, id, label = 'directory id', maxLength = 180) {
    const invalid = () => Object.assign(new Error(`invalid ${label}`), { statusCode: 400 });
    if (!validDirectoryId(id, maxLength)) throw invalid();
    const base = path.resolve(root);
    const target = path.resolve(base, id);
    if (!target.startsWith(base + path.sep)) throw invalid();
    try {
        if (fs.lstatSync(target).isSymbolicLink()) throw invalid();
    } catch (error) {
        if (error.code !== 'ENOENT') throw error;
    }
    return target;
}

module.exports = { childDirectory, validDirectoryId };
