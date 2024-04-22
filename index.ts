import { readdir, stat, rename, unlink } from 'fs/promises'
import { join, extname } from 'path'
import { spawn } from 'child_process'

import { fnv1 } from './fnvjs/src/index.js'

const UNPACKER_PATH = join(import.meta.dirname, 'Wwise-Unpacker')
const UNPACKER = join(UNPACKER_PATH, 'unpack_wav.bat')
const WAV_PATH = join(UNPACKER_PATH, 'dest_wav')
const WEM_PATH = join(UNPACKER_PATH, 'dest_raw')
export const PCK_DESTINATION = join(UNPACKER_PATH, 'Game_Files')

const random = () => Math.random().toString(36).substring(2, 15) + Math.random().toString(36).substring(2, 15)

const movePCK = async (folder: string) => {
  let i = 0
  console.log(`Reading ${folder}...`)
  const files = await readdir(folder)
  for (const file of files) {
    const filePath = join(folder, file)
    const fileStat = await stat(filePath)
    if (fileStat.isDirectory()) {
      i += await movePCK(filePath)
    } else {
      if (extname(file) === '.pck') {
        i++
        const newFile = random() + '.pck'
        await rename(filePath, join(PCK_DESTINATION, newFile))
      }
    }
  }
  return i
}

export const importPCK = async (folder: string) => {
  const i = await movePCK(folder)
  console.log(`Moved ${i} files.`)
}

export const cleanPCK = async () => {
  let i = 0
  const files = await readdir(PCK_DESTINATION)
  for (const file of files) {
    if (extname(file) === '.pck') {
      i++
      await unlink(join(PCK_DESTINATION, file))
    }
  }
  console.log(`Removed ${i} files.`)
}

export const unpack = () => new Promise<void>((resolve, reject) => {
  const child = spawn(UNPACKER, { cwd: UNPACKER_PATH, stdio: 'inherit' })

  child.on('close', code => {
    if (code === 0) {
      resolve()
    } else {
      reject(code)
    }
  })
})

export const exportFiles = async (folder: string) => {
  await rename(WAV_PATH, join(folder, 'wav'))
  await rename(WEM_PATH, join(folder, 'wem'))
  console.log(`Exported files to ${folder}.`)
}


export const encodeFNV64 = (input: string) => fnv1(input.toLowerCase(), 64).toString(16).padStart(16, '0')
