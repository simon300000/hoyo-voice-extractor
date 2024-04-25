import { readdir, stat, mkdir } from 'fs/promises'
import { join, extname, dirname } from 'path'

import { fnv1 } from './fnvjs/src/index.js'

import { parallelExecute, unpackPCK, convertWEM } from './tools.js'

const random = () => Math.random().toString(36).substring(2, 15) + Math.random().toString(36).substring(2, 15)

export const checkResultFolder = async (folder: string) => {
  const files = await readdir(folder)
  const no = ['wav', 'wem']
  const badFiles = no.filter(f => files.includes(f))
  if (badFiles.length) {
    console.log('Result folder already exists.')
    console.log(`Please remove the following files/folders from ${folder}:`)
    console.log(badFiles.join('\n'))
    process.exit(1)
  }
}

export const findFiles = async (folder: string, ext: string) => {
  const files = await readdir(folder)
  const result = [] as string[]
  const resultPromise = [] as Promise<string[]>[]
  for (const file of files) {
    const filePath = join(folder, file)
    const fileStat = await stat(filePath)
    if (fileStat.isDirectory()) {
      resultPromise.push(findFiles(filePath, ext))
    } else {
      if (extname(file) === ext) {
        result.push(filePath)
      }
    }
  }
  return [...result, ...(await Promise.all(resultPromise)).flat()]
}

export const unpack = async (source: string, wemFolder: string, wavFolder: string) => {
  await mkdir(wemFolder, { recursive: true })
  await mkdir(wavFolder, { recursive: true })
  const pcks = await findFiles(source, '.pck')
  console.log(`Found ${pcks.length} pck files.`)
  console.log('Unpacking...')
  const wavCreatedFolders = new Set<string>()
  await parallelExecute(pcks.map(pck => async () => {
    const name = random()
    const wem = join(wemFolder, name)
    const wav = join(wavFolder, name)
    await mkdir(wem)
    await mkdir(wav)
    wavCreatedFolders.add(wav)
    await unpackPCK(pck, wem)
  }))
  const wems = await findFiles(wemFolder, '.wem')
  console.log(`Found ${wems.length} wem files.`)
  console.log('Converting...')
  for (const wem of wems) {
    const folder = dirname(wem).replace(wemFolder, wavFolder)
    if (!wavCreatedFolders.has(folder)) {
      await mkdir(folder, { recursive: true })
      wavCreatedFolders.add(folder)
    }
  }
  await parallelExecute(wems.map(wem => async () => {
    const wavPath = wem.replace(wemFolder, wavFolder).replace('.wem', '.wav')
    await convertWEM(wem, wavPath)
  }), 1)
}

export const encodeFNV64 = (input: string) => fnv1(input.toLowerCase(), 64).toString(16).padStart(16, '0')
