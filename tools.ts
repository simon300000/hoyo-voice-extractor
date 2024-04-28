import { cpus } from 'os'
import { join, dirname } from 'path'
import { spawn } from 'child_process'

const UNPACKER_PATH = join(import.meta.dirname, 'Wwise-Unpacker')
const TOOLS_PATH = join(UNPACKER_PATH, 'Tools')
const QUICKBMS = join(TOOLS_PATH, 'quickbms.exe')
const WWISE_PCK_EXTRACTOR = join(TOOLS_PATH, 'wwise_pck_extractor.bms')
const VGMSTREAM_CLI = join(TOOLS_PATH, 'vgmstream-cli.exe')
const BNKEXTR = join(TOOLS_PATH, 'bnkextr.exe')

export const parallelExecute = async (tasks: (() => Promise<void>)[], multiplier = 2) => {
  const cpusCount = cpus().length * multiplier
  const workers = [] as Promise<void>[]
  let taskDone = 0
  const taskDiv10 = Math.floor(tasks.length / 10)
  for (let i = 0; i < cpusCount; i++) {
    workers.push((async () => {
      while (tasks.length) {
        await tasks.shift()?.()
        taskDone++
        if (taskDone % taskDiv10 === 0) {
          console.log(`${Math.floor(taskDone / taskDiv10) * 10}%`)
        }
      }
    })())
  }
  await Promise.all(workers)
}

const execute = (command: string, args: string[], cwd?: string) => new Promise<void>((resolve, reject) => {
  const process = spawn(command, args, { cwd })
  let stdout = ''
  let stderr = ''
  process.stdout.on('data', data => stdout += data)
  process.stderr.on('data', data => stderr += data)
  process.on('close', code => {
    if (code === 0) {
      resolve()
    } else {
      reject(new Error(`${command} exited with code ${code}\n${stdout}\n${stderr}`))
    }
  })
})

export const unpackPCK = async (pck: string, output: string) => execute(QUICKBMS, ['-q', '-k', WWISE_PCK_EXTRACTOR, pck, output])

export const convertWEM = async (wem: string, wav: string) => execute(VGMSTREAM_CLI, ['-o', wav, wem])

export const unpackBNK = async (bnk: string) => execute(BNKEXTR, [bnk])
