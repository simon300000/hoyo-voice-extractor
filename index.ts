import { readdir, stat, mkdir, readFile, writeFile } from 'fs/promises'
import { join, extname, dirname } from 'path'

import { fnv1 } from './fnvjs/src/index.js'

import { parallelExecute, unpackPCK, convertWEM, unpackBNK } from './tools.js'

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

export const findWAV = async (path: string) => findFiles(path, '.wav')
export const findJSON = async (path: string) => findFiles(path, '.json')

export const readJSON = async <T>(path: string) => JSON.parse(await readFile(path, 'utf8')) as T
export const readJSONs = async <T>(paths: string[]) => {
  const result = [] as T[]
  while (paths.length) {
    const batch = paths.splice(0, 256)
    result.push(...await Promise.all(batch.map(readJSON<T>)))
  }
  return result
}

export const createFolders = async (files: string[], existing = new Set<string>()) => {
  const folders = new Set<string>(existing)
  for (const file of files) {
    const folder = dirname(file)
    if (!folders.has(folder)) {
      await mkdir(folder, { recursive: true })
      folders.add(folder)
    }
  }
}

const convertWEMs = async (wems: string[], wemFolder: string, wavFolder: string) => {
  await createFolders(wems.map(wem => wem.replace(wemFolder, wavFolder)))
  await parallelExecute(wems.map(wem => async () => {
    const wavPath = wem.replace(wemFolder, wavFolder).replace('.wem', '.wav')
    await convertWEM(wem, wavPath)
  }), 1)
}

export const unpackPCKs = async (source: string, wemFolder: string, wavFolder: string) => {
  await mkdir(wemFolder, { recursive: true })
  await mkdir(wavFolder, { recursive: true })
  const pcks = await findFiles(source, '.pck')
  console.log(`Found ${pcks.length} pck files.`)
  console.log('Unpacking...')
  await parallelExecute(pcks.map(pck => async () => {
    const name = random()
    const wem = join(wemFolder, name)
    const wav = join(wavFolder, name)
    await mkdir(wem)
    await mkdir(wav)
    await unpackPCK(pck, wem)
  }))
  const wems = await findFiles(wemFolder, '.wem')
  console.log(`Found ${wems.length} wem files.`)
  console.log('Converting...')
  await convertWEMs(wems, wemFolder, wavFolder)
}

export const unpackBNKs = async (source: string, wavFolder: string) => {
  const bnks = await findFiles(source, '.bnk')
  console.log(`Found ${bnks.length} bnk files.`)
  console.log('Unpacking...')
  await parallelExecute(bnks.map(bnk => async () => unpackBNK(bnk)))

  const wems = await findFiles(source, '.wem')
  console.log(`Found ${wems.length} wem files.`)
  console.log('Converting...')
  await convertWEMs(wems, source, wavFolder)
}

export const encodeFNV64 = (input: string) => fnv1(input.toLowerCase(), 64).toString(16).padStart(16, '0')

export const readTextMap = async <TextMapMap extends Record<string, string>>(dataPath: string, textMapMap: TextMapMap) => {
  return Object.fromEntries(await Promise.all(Object.entries(textMapMap).map(async ([language, filename]) => [language, await readJSON(join(dataPath, 'TextMap', filename))]))) as Record<keyof TextMapMap, TextMap>
}

export const updateStats = async <Voice extends VoiceBase>(voiceMap: Record<string, Voice>, readme: string) => {
  const currentDate = new Date().toISOString().replace(/T.*/, '')

  let noSpeaker = 0
  let noText = 0
  let noFileName = 0

  for (const voice of Object.values(voiceMap)) {
    if (!voice.speaker) {
      noSpeaker++
    }
    if (!voice.transcription) {
      noText++
    }
    if (!voice.inGameFilename) {
      noFileName++
    }
  }

  const stats = `<!-- STATS -->
Last update at \`${currentDate}\`

\`${Object.keys(voiceMap).length}\` wavs

\`${noSpeaker}\` without speaker

\`${noText}\` without transcription

\`${noFileName}\` without inGameFilename
<!-- STATS_END -->`

  console.log(stats)

  const originalReadme = await readFile(readme, 'utf-8')
  const updatedReadme = originalReadme.replace(/<!-- STATS -->[\s\S]*<!-- STATS_END -->/, stats)
  await writeFile('readme.md', updatedReadme)
}

export const copyReadme = async (source: string, destination: string, huggingfaceMetadata: string) => {
  const readme = await readFile(source, 'utf-8')
  await writeFile(destination, `${huggingfaceMetadata}\n\n${readme}`)
}

type TextMap = Record<string, string>

type VoiceBase = {
  inGameFilename: string
  language: string
  transcription: string
  speaker: string
}
